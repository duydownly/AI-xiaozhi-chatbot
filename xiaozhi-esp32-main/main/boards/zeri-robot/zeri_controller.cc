#include "zeri_movements.h"
#include "mcp_server.h"
#include "application.h"
#include "settings.h"
#include "board.h"
#include "wifi_station.h"
#include "config.h"
#include <cJSON.h>
#include <cstring>
#include <esp_log.h>

#define TAG "ZeriController"

class ZeriController {
private:
    Zeri zeri_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool is_action_in_progress_ = false;

    struct ActionParams {
        int type;
        int steps;
        int speed;
        int direction;
        int amount;
        char sequence_json[512];
    };

    enum ActionType {
        ACTION_WALK = 1,
        ACTION_TURN = 2,
        ACTION_SIT = 3,
        ACTION_SWING = 4,
        ACTION_SHAKE_TAIL = 5,
        ACTION_HOME = 6,
        ACTION_SERVO_SEQUENCE = 7
    };

    //-------------------------------------------------------
    // ACTION TASK
    //-------------------------------------------------------
    static void ActionTask(void* arg) {
        ZeriController* c = static_cast<ZeriController*>(arg);
        ActionParams params;

        c->zeri_.AttachServos();

        while (true) {
            if (xQueueReceive(c->action_queue_, &params, pdMS_TO_TICKS(1000)) == pdTRUE) {
                c->is_action_in_progress_ = true;

                switch (params.type) {
                case ACTION_WALK:
                    c->zeri_.Walk(params.steps, params.speed, params.direction);
                    break;

                case ACTION_TURN:
                    c->zeri_.Turn(params.steps, params.speed, params.direction);
                    break;

                case ACTION_SIT:
                    c->zeri_.Sit();
                    break;

                case ACTION_SWING:
                    c->zeri_.Swing(params.steps, params.speed, params.amount);
                    break;

                case ACTION_SHAKE_TAIL:
                    c->zeri_.ShakeTail(params.steps, params.speed, params.amount);
                    break;

                case ACTION_HOME:
                    c->zeri_.Home();
                    break;

                case ACTION_SERVO_SEQUENCE:
                    c->ExecuteSequence(params.sequence_json);
                    break;
                }

                c->is_action_in_progress_ = false;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    //-------------------------------------------------------
    void ExecuteSequence(const char* json_str) {
        const char* servo_names[] = { "ll", "rl", "bl", "br", "ta" };
        int current[5] = {90,90,90,90,90};

        cJSON* root = cJSON_Parse(json_str);
        if (!root) return;

        cJSON* actions = cJSON_GetObjectItem(root, "a");
        if (!cJSON_IsArray(actions)) {
            cJSON_Delete(root);
            return;
        }

        int count = cJSON_GetArraySize(actions);
        for (int i = 0; i < count; i++) {
            cJSON* item = cJSON_GetArrayItem(actions, i);
            int target[5];
            memcpy(target, current, sizeof(target));

            cJSON* s = cJSON_GetObjectItem(item, "s");
            if (cJSON_IsObject(s)) {
                for (int j = 0; j < 5; j++) {
                    cJSON* v = cJSON_GetObjectItem(s, servo_names[j]);
                    if (cJSON_IsNumber(v)) target[j] = v->valueint;
                }
            }

            int speed = 1000;
            cJSON* v = cJSON_GetObjectItem(item, "v");
            if (cJSON_IsNumber(v)) speed = v->valueint;

            zeri_.MoveServos(speed, target);
            memcpy(current, target, sizeof(target));

            cJSON* d = cJSON_GetObjectItem(item, "d");
            if (cJSON_IsNumber(d))
                vTaskDelay(pdMS_TO_TICKS(d->valueint));
        }

        cJSON_Delete(root);
    }

    //-------------------------------------------------------
    void QueueAction(int type, int steps = 1, int speed = 1000, int direction = 1, int amount = 30) {
        ActionParams p{};
        p.type = type;
        p.steps = steps;
        p.speed = speed;
        p.direction = direction;
        p.amount = amount;

        xQueueSend(action_queue_, &p, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void QueueSequence(const char* json) {
        ActionParams p{};
        p.type = ACTION_SERVO_SEQUENCE;

        strncpy(p.sequence_json, json, sizeof(p.sequence_json)-1);
        xQueueSend(action_queue_, &p, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void StartActionTaskIfNeeded() {
        if (!action_task_handle_) {
            xTaskCreate(ActionTask, "zeri_action", 4096, this,
                        configMAX_PRIORITIES-2, &action_task_handle_);
        }
    }

public:
    //-------------------------------------------------------
    ZeriController() {
        zeri_.Init(
            FRONT_LEFT_LEG_PIN,
            FRONT_RIGHT_LEG_PIN,
            BACK_LEFT_LEG_PIN,
            BACK_RIGHT_LEG_PIN,
            TAIL_PIN
        );

        action_queue_ = xQueueCreate(10, sizeof(ActionParams));
        QueueAction(ACTION_HOME);

        RegisterTools();
        ESP_LOGI(TAG, "Zeri controller initialized");
    }

    //-------------------------------------------------------
    void RegisterTools() {
        auto& m = McpServer::GetInstance();
        //Get ip
        // GET WIFI IP
    m.AddTool("self.zeri.get_ip_address",
    "Return current Wi-Fi IP address",
    PropertyList(),
    [](const PropertyList&)->ReturnValue {
        return WifiStation::GetInstance().GetIpAddress();
    }
);

        // BASIC ACTIONS

        m.AddTool("self.zeri.action",
            "walk / turn / sit / swing / shake_tail / home",
            PropertyList({
                Property("action", kPropertyTypeString, "walk"),
                Property("steps",  kPropertyTypeInteger, 3),
                Property("speed",  kPropertyTypeInteger, 700),
                Property("direction", kPropertyTypeInteger, 1),
                Property("amount", kPropertyTypeInteger, 30)
            }),
            [this](const PropertyList& p)->ReturnValue {

                std::string a = p["action"].value<std::string>();
                int steps = p["steps"].value<int>();
                int speed = p["speed"].value<int>();
                int direction = p["direction"].value<int>();
                int amount = p["amount"].value<int>();

                if (a == "walk") QueueAction(ACTION_WALK, steps, speed, direction);
                else if (a == "turn") QueueAction(ACTION_TURN, steps, speed, direction);
                else if (a == "sit") QueueAction(ACTION_SIT);
                else if (a == "swing") QueueAction(ACTION_SWING, steps, speed, 1, amount);
                else if (a == "shake_tail") QueueAction(ACTION_SHAKE_TAIL, steps, speed, 1, amount);
                else if (a == "home") QueueAction(ACTION_HOME);
                else return "unknown action";

                return true;
            });

        // SERVO SEQUENCE
        m.AddTool("self.zeri.servo_sequences",
            "Custom servo sequence for ll/rl/bl/br/ta",
            PropertyList({ Property("sequence", kPropertyTypeString, "") }),
            [this](const PropertyList& p)->ReturnValue {
                QueueSequence(p["sequence"].value<std::string>().c_str());
                return true;
            });

        m.AddTool("self.zeri.get_status", "moving / idle", PropertyList(),
            [this](const PropertyList&)->ReturnValue {
                return is_action_in_progress_ ? "moving" : "idle";
            });
    }
};

static ZeriController* g_controller = nullptr;

void InitializeZeriController() {
    if (!g_controller) g_controller = new ZeriController();
}
