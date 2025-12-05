/*
    Puppy Robot Controller - Clean 5 Servo Version
    (4 legs + tail)
*/

#include <cJSON.h>
#include <esp_log.h>

#include <cstdlib>
#include <cstring>

#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "puppy_movements.h"
#include "sdkconfig.h"
#include "settings.h"

#define TAG "PuppyController"

class PuppyController {
private:
    Puppy puppy_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool is_action_in_progress_ = false;

    struct PuppyActionParams {
        int action_type;
        int steps;
        int speed;
        int direction;
        int amount;
        char servo_sequence_json[512];
    };

    enum ActionType {
        ACTION_WALK = 1,
        ACTION_TURN = 2,
        ACTION_JUMP = 3,
        ACTION_SWING = 4,
        ACTION_MOONWALK = 5,
        ACTION_SIT = 6,
        ACTION_UPDOWN = 7,
        ACTION_HOME = 8,

        ACTION_SERVO_SEQUENCE = 20,

        ACTION_TAIL_WAG = 40,
        ACTION_TAIL_HAPPY = 41,
        ACTION_TAIL_SAD = 42,
    };

    /* ============================================================
       ACTION TASK
       ============================================================ */
    static void ActionTask(void* arg) {
        auto* controller = static_cast<PuppyController*>(arg);
        PuppyActionParams params;

        controller->puppy_.AttachServos();

        while (true) {
            if (xQueueReceive(controller->action_queue_, &params, portMAX_DELAY) != pdTRUE)
                continue;

            ESP_LOGI(TAG, "Dequeued action: type=%d steps=%d speed=%d dir=%d amt=%d",
                     params.action_type, params.steps, params.speed, params.direction, params.amount);

            controller->is_action_in_progress_ = true;

            if (params.action_type == ACTION_SERVO_SEQUENCE) {
                controller->HandleServoSequence(params.servo_sequence_json);
                controller->is_action_in_progress_ = false;
                continue;
            }

            switch (params.action_type) {

                case ACTION_WALK:
                    controller->puppy_.Walk(params.steps, params.speed, params.direction);
                    break;

                case ACTION_TURN:
                    controller->puppy_.Turn(params.steps, params.speed, params.direction);
                    break;

                case ACTION_JUMP:
                    controller->puppy_.Jump(params.steps, params.speed);
                    break;

                case ACTION_SWING:
                    controller->puppy_.Swing(params.steps, params.speed, params.amount);
                    break;

                case ACTION_MOONWALK:
                    controller->puppy_.Moonwalker(params.steps, params.speed,
                                                  params.amount, params.direction);
                    break;

                case ACTION_SIT:
                    controller->puppy_.Sit();
                    break;

                case ACTION_UPDOWN:
                    controller->puppy_.UpDown(params.steps, params.speed, params.amount);
                    break;

                case ACTION_TAIL_WAG:
                    controller->TailWag(params.amount, params.speed);
                    break;

                case ACTION_TAIL_HAPPY:
                    controller->TailHappy();
                    break;

                case ACTION_TAIL_SAD:
                    controller->TailSad();
                    break;

                case ACTION_HOME:
                    controller->puppy_.Home();
                    break;
            }

            controller->is_action_in_progress_ = false;
        }
    }

    /* ============================================================
       START ACTION TASK
       ============================================================ */
    void StartActionTaskIfNeeded() {
        if (action_task_handle_ == nullptr) {
            xTaskCreate(ActionTask, "puppy_action", 4096, this,
                        configMAX_PRIORITIES - 2, &action_task_handle_);
        }
    }

    /* ============================================================
       LOAD TRIMS
       ============================================================ */
    void LoadTrimsFromNVS() {
        Settings settings("puppy_trims", false);

        int ll = settings.GetInt("left_leg", 0);
        int rl = settings.GetInt("right_leg", 0);
        int lf = settings.GetInt("left_foot", 0);
        int rf = settings.GetInt("right_foot", 0);
        int tail = settings.GetInt("tail", 0);

        puppy_.SetTrims(ll, rl, lf, rf, tail);
    }

    /* ============================================================
       SERVO SEQUENCE HANDLER (AI custom actions)
       ============================================================ */
 void HandleServoSequence(const char* json_str) {
    if (!json_str || strlen(json_str) == 0)
        return;
    int json_len = (int)strlen(json_str);
    ESP_LOGI(TAG, "HandleServoSequence: raw len=%d json='%.128s'", json_len, json_str);

    int hex_print = std::min(json_len, 64);
    char hexbuf[256];
    int hp = 0;
    for (int i = 0; i < hex_print && hp < (int)sizeof(hexbuf)-4; ++i) {
        hp += snprintf(hexbuf + hp, sizeof(hexbuf) - hp, "%02X ", (uint8_t)json_str[i]);
    }
    hexbuf[hp] = '\0';
    ESP_LOGI(TAG, "HandleServoSequence: raw hex (first %d bytes): %s", hex_print, hexbuf);

    cJSON* json = cJSON_Parse(json_str);
    if (!json) return;

    cJSON* actions = cJSON_GetObjectItem(json, "a");
    if (!cJSON_IsArray(actions)) {
        cJSON_Delete(json);
        return;
    }

    const char* names[5] = {"ll", "rl", "lf", "rf", "tail"};
    int pos[5] = {90,90,90,90,90};

    int count = cJSON_GetArraySize(actions);

    for (int i = 0; i < count; i++) {
        cJSON* act = cJSON_GetArrayItem(actions, i);

        /* ==========================================================
           AUTO-DETECT TAIL WAG TRIGGER
           Nếu JSON có "id":5 hoặc liên quan đến tail → tự vẫy đuôi
           ========================================================== */
        cJSON* idv = cJSON_GetObjectItem(act, "id");
        if (cJSON_IsNumber(idv)) {

            int id = idv->valueint;

            /* Nếu AI dùng id=5 cho tail thì để 5, nếu id=4 thì sửa lại */
            if (id == 5) {
                ESP_LOGI(TAG, "AUTO TailWag triggered (id==5) from servo_sequence");

                /* Vẫy 2 lần nhẹ, speed 200 */
                puppy_.TailWag(10, 100);

                cJSON_Delete(json);
                return;
            }
        }

        /* ==========================================================
           OSC MODE
           ========================================================== */
        cJSON* osc = cJSON_GetObjectItem(act, "osc");
        if (cJSON_IsObject(osc)) {

            int A[5] = {0};
            int C[5] = {90,90,90,90,90};
            double P[5] = {0};
            int period = 400;
            float cyc = 5;

            cJSON* a = cJSON_GetObjectItem(osc, "a");
            if (cJSON_IsObject(a)) {
                for (int j = 0; j < 5; j++) {
                    cJSON* v = cJSON_GetObjectItem(a, names[j]);
                    if (cJSON_IsNumber(v)) A[j] = v->valueint;
                }
            }

            cJSON* o = cJSON_GetObjectItem(osc, "o");
            if (cJSON_IsObject(o)) {
                for (int j = 0; j < 5; j++) {
                    cJSON* v = cJSON_GetObjectItem(o, names[j]);
                    if (cJSON_IsNumber(v)) C[j] = v->valueint;
                }
            }

            cJSON* ph = cJSON_GetObjectItem(osc, "ph");
            if (cJSON_IsObject(ph)) {
                for (int j = 0; j < 5; j++) {
                    cJSON* v = cJSON_GetObjectItem(ph, names[j]);
                    if (cJSON_IsNumber(v))
                        P[j] = v->valuedouble * 3.1415926535 / 180.0;
                }
            }

            cJSON* p = cJSON_GetObjectItem(osc, "p");
            if (cJSON_IsNumber(p)) period = p->valueint;

            cJSON* c = cJSON_GetObjectItem(osc, "c");
            if (cJSON_IsNumber(c)) cyc = c->valuedouble;

            ESP_LOGI(TAG, "HandleServoSequence: OSC action: period=%d cyc=%f", period, cyc);
            puppy_.Execute2(A, C, period, P, cyc);

            memcpy(pos, C, sizeof(pos));
        }

        /* ==========================================================
           DIRECT MOVE
           ========================================================== */
        else {
            int target[5];
            memcpy(target, pos, sizeof(pos));

            cJSON* s = cJSON_GetObjectItem(act, "s");
            if (cJSON_IsObject(s)) {
                for (int j = 0; j < 5; j++) {
                    cJSON* v = cJSON_GetObjectItem(s, names[j]);
                    if (cJSON_IsNumber(v)) target[j] = v->valueint;
                }
            }

            int speed = 800;
            cJSON* v = cJSON_GetObjectItem(act, "v");
            if (cJSON_IsNumber(v)) speed = v->valueint;

            ESP_LOGI(TAG, "HandleServoSequence: DIRECT move targets=%d,%d,%d,%d,%d speed=%d",
                     target[0], target[1], target[2], target[3], target[4], speed);

            puppy_.MoveServos(speed, target);
            memcpy(pos, target, sizeof(pos));
        }

        cJSON* delay = cJSON_GetObjectItem(act, "d");
        if (cJSON_IsNumber(delay))
            vTaskDelay(pdMS_TO_TICKS(delay->valueint));
    }

    cJSON_Delete(json);
}

    /* ============================================================
       TAIL ACTIONS
       ============================================================ */
    void TailWag(int amount, int speed) {
        int times = amount > 0 ? amount : 4;
        int sp = speed > 0 ? speed : 300;

        /* For quick testing, cap the number of wags so we can observe behavior */
        int capped = std::min(times, 2);
        ESP_LOGI(TAG, "TailWag wrapper called: requested=%d capped=%d speed=%d", times, capped, sp);

        /* Move to neutral briefly then perform capped wag */
        int center[SERVO_COUNT] = {90, 90, 90, 90, 90};
        puppy_.MoveServos(200, center);
        puppy_.TailWag(capped, sp);
    }

    void TailHappy() {
        int t[5] = {90,90,90,90,130};
        puppy_.MoveServos(350, t);
        TailWag(35, 250);
    }

    void TailSad() {
        int t[5] = {90,90,90,90,40};
        puppy_.MoveServos(400, t);
    }

    /* ============================================================
       REGISTER MCP TOOLS
       ============================================================ */
    void RegisterMcpTools() {
        auto& m = McpServer::GetInstance();

        m.AddTool("self.puppy.action", "robot basic motion",
            PropertyList({
                Property("action",  kPropertyTypeString, "walk"),
                Property("steps",   kPropertyTypeInteger, 1, 1, 50),
                Property("speed",   kPropertyTypeInteger, 800, 100, 3000),
                Property("direction", kPropertyTypeInteger, 1, -1, 1),
                Property("amount",  kPropertyTypeInteger, 20, 0, 90)
            }),
            [this](const PropertyList& p)->ReturnValue {

                std::string a = p["action"].value<std::string>();
                ESP_LOGI(TAG, "MCP tool self.puppy.action called: %s", a.c_str());
                int steps = p["steps"].value<int>();
                int speed = p["speed"].value<int>();
                int dir   = p["direction"].value<int>();
                int amt   = p["amount"].value<int>();

                if (a == "walk")       QueueAction(ACTION_WALK, steps, speed, dir, amt);
                else if (a == "turn")  QueueAction(ACTION_TURN, steps, speed, dir, 0);
                else if (a == "jump")  QueueAction(ACTION_JUMP, steps, speed, 0, 0);
                else if (a == "swing") QueueAction(ACTION_SWING, steps, speed, 0, amt);
                else if (a == "moon")  QueueAction(ACTION_MOONWALK, steps, speed, dir, amt);
                else if (a == "sit")   QueueAction(ACTION_SIT, 1, 0, 0, 0);
                else if (a == "up")    QueueAction(ACTION_UPDOWN, steps, speed, 0, amt);
                else if (a == "home")  QueueAction(ACTION_HOME, steps, speed, dir, amt);
                else if (a == "tail_wag")   QueueAction(ACTION_TAIL_WAG, steps, speed, 0, amt);
                else if (a == "tail_happy") QueueAction(ACTION_TAIL_HAPPY, 1, 0, 0, 0);
                else if (a == "tail_sad")   QueueAction(ACTION_TAIL_SAD, 1, 0, 0, 0);
                else return "Invalid action";

                return true;
        });

        m.AddTool("self.puppy.servo_sequence",
            "AI custom servo motion",
            PropertyList({
                Property("json", kPropertyTypeString, "{\"a\":[]}")
            }),
            [this](const PropertyList& p)->ReturnValue {
                std::string js = p["json"].value<std::string>();
                ESP_LOGI(TAG, "MCP tool self.puppy.servo_sequence called (len=%d)", (int)js.length());
                QueueServoSequence(js.c_str());
                return true;
            });

        m.AddTool("self.puppy.home", "reset", PropertyList(),
            [this](const PropertyList&)->ReturnValue {
                QueueAction(ACTION_HOME, 1, 800, 0, 0);
                return true;
        });
    }


public:  
    /* ============================================================
       CONSTRUCTOR
       ============================================================ */
    PuppyController() {

        puppy_.Init(
            LEFT_LEG_PIN,
            RIGHT_LEG_PIN,
            LEFT_FOOT_PIN,
            RIGHT_FOOT_PIN,
            TAIL_SERVO_PIN
        );

        action_queue_ = xQueueCreate(10, sizeof(PuppyActionParams));

        /* Make sure the action task is running and servos attached early so
           the initial HOME (and any incoming actions) are executed. */
        StartActionTaskIfNeeded();
        puppy_.AttachServos();

        LoadTrimsFromNVS();

        RegisterMcpTools();

        /* Queue initial home after tools are registered */
        QueueAction(ACTION_HOME, 1, 800, 0, 0);
    }

    /* ============================================================
       ACTION QUEUE
       ============================================================ */
    void QueueAction(int type, int steps, int speed, int direction, int amount) {
        PuppyActionParams p{};
        p.action_type = type;
        p.steps = steps;
        p.speed = speed;
        p.direction = direction;
        p.amount = amount;

        StartActionTaskIfNeeded();
        ESP_LOGI(TAG, "QueueAction queued: type=%d steps=%d speed=%d dir=%d amt=%d",
                 p.action_type, p.steps, p.speed, p.direction, p.amount);
        xQueueSend(action_queue_, &p, portMAX_DELAY);
    }

    void QueueServoSequence(const char* json_str) {
        PuppyActionParams p{};
        p.action_type = ACTION_SERVO_SEQUENCE;
        strncpy(p.servo_sequence_json, json_str, sizeof(p.servo_sequence_json)-1);

        StartActionTaskIfNeeded();
        ESP_LOGI(TAG, "QueueServoSequence queued (len=%d)", (int)strlen(p.servo_sequence_json));
        xQueueSend(action_queue_, &p, portMAX_DELAY);
    }
};  // <-- THIS WAS MISSING

/* ============================================================
   GLOBAL ENTRY
   ============================================================ */
static PuppyController* g_puppy_controller = nullptr;

void InitializePuppyController() {
    if (!g_puppy_controller) {
        g_puppy_controller = new PuppyController();
        ESP_LOGI(TAG, "Puppy Controller Initialized");
    }
}
