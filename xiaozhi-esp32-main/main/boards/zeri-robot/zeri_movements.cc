#include "zeri_movements.h"

#include "freertos/idf_additions.h"
#include "oscillator.h"
#include <algorithm>
#include <cmath>

static const char* TAG = "ZeriMovements";

Zeri::Zeri() {
    is_zeri_resting_ = false;

    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_pins_[i] = -1;
        servo_trim_[i] = 0;
    }
}

Zeri::~Zeri() {
    DetachServos();
}

unsigned long IRAM_ATTR millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

///////////////////////////////////////////////////////////
// INIT
///////////////////////////////////////////////////////////

void Zeri::Init(int front_left_leg,
                int front_right_leg,
                int back_left_leg,
                int back_right_leg,
                int tail_servo)
{
    servo_pins_[FRONT_LEFT_LEG]  = front_left_leg;
    servo_pins_[FRONT_RIGHT_LEG] = front_right_leg;
    servo_pins_[BACK_LEFT_LEG]   = back_left_leg;
    servo_pins_[BACK_RIGHT_LEG]  = back_right_leg;
    servo_pins_[TAIL_SERVO]      = tail_servo;

    AttachServos();
    is_zeri_resting_ = false;
}

///////////////////////////////////////////////////////////
// SERVO ATTACH / DETACH
///////////////////////////////////////////////////////////

void Zeri::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Attach(servo_pins_[i]);
        }
    }
}

void Zeri::DetachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Detach();
        }
    }
}

///////////////////////////////////////////////////////////
// TRIM
///////////////////////////////////////////////////////////

void Zeri::SetTrims(int fl, int fr, int bl, int br, int tail) {
    servo_trim_[FRONT_LEFT_LEG]  = fl;
    servo_trim_[FRONT_RIGHT_LEG] = fr;
    servo_trim_[BACK_LEFT_LEG]   = bl;
    servo_trim_[BACK_RIGHT_LEG]  = br;
    servo_trim_[TAIL_SERVO]      = tail;

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetTrim(servo_trim_[i]);
        }
    }
}

///////////////////////////////////////////////////////////
// BASIC MOVE
///////////////////////////////////////////////////////////

void Zeri::MoveServos(int time, int servo_target[]) {
    if (GetRestState()) {
        SetRestState(false);
    }

    final_time_ = millis() + time;

    if (time > 10) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                increment_[i] = (servo_target[i] - servo_[i].GetPosition()) / (time / 10.0);
            }
        }

        while (millis() < final_time_) {
            for (int i = 0; i < SERVO_COUNT; i++) {
                if (servo_pins_[i] != -1) {
                    servo_[i].SetPosition(servo_[i].GetPosition() + increment_[i]);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                servo_[i].SetPosition(servo_target[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(time));
    }

    // Final snap correction
    for (int retry = 0; retry < 3; retry++) {
        bool ok = true;
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1 &&
                servo_target[i] != servo_[i].GetPosition()) {
                ok = false;
                break;
            }
        }
        if (ok) break;

        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                servo_[i].SetPosition(servo_target[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

///////////////////////////////////////////////////////////
// HOME POSITION
///////////////////////////////////////////////////////////

void Zeri::Home() {
    int home[SERVO_COUNT] = {
        90, // FL
        90, // FR
        90, // BL
        90, // BR
        90  // tail
    };

    MoveServos(600, home);
    is_zeri_resting_ = true;
    vTaskDelay(pdMS_TO_TICKS(200));
}

bool Zeri::GetRestState() { return is_zeri_resting_; }
void Zeri::SetRestState(bool s) { is_zeri_resting_ = s; }

///////////////////////////////////////////////////////////
// WALK
///////////////////////////////////////////////////////////
void Zeri::Walk(float steps, int period, int direction)
{
    // Biên độ dao động của chân trước
    int A[SERVO_COUNT] = {
        25,  // FL dao động nhẹ, thấp
        25,  // FR dao động nhẹ, thấp
        0,   // BL cố định
        0,   // BR cố định
        3    // tail nhẹ
    };

    // Offset mới:
    //  - Chân trước thấp xuống
    //  - Chân sau đứng thẳng (90°)
    int O[SERVO_COUNT] = {
        60,  // FL đặt thấp xuống
        120, // FR đặt thấp (đối xứng cơ khí)
        90,  // BL thẳng đứng
        90,  // BR thẳng đứng
        0
    };

    // Pha dao động:
    double P[SERVO_COUNT] = {
        DEG2RAD(0),       // FL
        DEG2RAD(180),     // FR
        0,                // BL cố định
        0,                // BR cố định
        0
    };

    Execute(A, O, period, P, steps);
}


///////////////////////////////////////////////////////////
// TURN
///////////////////////////////////////////////////////////

void Zeri::Turn(float steps, int period, int direction) {

    int A[SERVO_COUNT] = {
        (direction == LEFT) ? 30 : 5,
        (direction == RIGHT) ? 30 : 5,
        30,
        30,
        10
    };

    int O[SERVO_COUNT] = { 0, 0, 5, -5, 0 };

    double P[SERVO_COUNT] = { 0, 0, DEG2RAD(-90), DEG2RAD(-90), 0 };

    Execute(A, O, period, P, steps);
}

///////////////////////////////////////////////////////////
// SHAKE TAIL
///////////////////////////////////////////////////////////

void Zeri::ShakeTail(float steps, int period, int amplitude) {

    int A[SERVO_COUNT] = { 0, 0, 0, 0, amplitude };
    int O[SERVO_COUNT] = { 0, 0, 0, 0, 0 };
    double P[SERVO_COUNT] = { 0, 0, 0, 0, DEG2RAD(90) };

    Execute(A, O, period, P, steps);
}

///////////////////////////////////////////////////////////
// SIT
///////////////////////////////////////////////////////////

void Zeri::Sit() {

    // Giữ nguyên góc 2 chân trước
    int fl = servo_[FRONT_LEFT_LEG].GetPosition();
    int fr = servo_[FRONT_RIGHT_LEG].GetPosition();

    // Góc mục tiêu để ngồi
    int target_bl = 30;
    int target_br = 180 - 30;

    // Đuôi
    int tail_center = servo_[TAIL_SERVO].GetPosition();

    // Số bước chia nhỏ để chuyển động chậm
    const int steps = 20;
    const int delay_ms = 30;   // càng lớn càng chậm

    for (int i = 0; i <= steps; i++) {

        float k = (float)i / steps;  // từ 0 → 1

        int bl = fl + (target_bl - fl) * k;
        int br = fr + (target_br - fr) * k;

        // Lắc đuôi nhẹ nhẹ lúc đang ngồi (biên độ 5 độ)
        int tail = tail_center + sin(i * 0.4f) * 30;

        int t[SERVO_COUNT] = {
            fl,     // FL giữ nguyên
            fr,     // FR giữ nguyên
            bl,     // BL tiến dần đến target
            br,     // BR tiến dần đến target
            tail    // đuôi lắc lư
        };

        MoveServos(10, t);     // mỗi lần chỉnh 10ms
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    // Khi ngồi xong, đưa đuôi về giữa và đứng yên
    int t_end[SERVO_COUNT] = {
        fl,
        fr,
        target_bl,
        target_br,
        tail_center
    };

    MoveServos(300, t_end);
}


///////////////////////////////////////////////////////////
// SWING
///////////////////////////////////////////////////////////

void Zeri::Swing(float steps, int period, int height) {

    int A[SERVO_COUNT] = { 0, 0, height, height, height / 2 };
    int O[SERVO_COUNT] = { 0, 0, height / 2, -height / 2, 0 };
    double P[SERVO_COUNT] = { 0, 0, 0, 0, 0 };

    Execute(A, O, period, P, steps);
}

///////////////////////////////////////////////////////////
// EXECUTION CORE
///////////////////////////////////////////////////////////

void Zeri::OscillateServos(int A[], int O[], int T,
                           double phase_diff[], float cycles)
{
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetA(A[i]);
            servo_[i].SetO(O[i]);
            servo_[i].SetT(T);
            servo_[i].SetPh(phase_diff[i]);
        }
    }

    double start = millis();
    double end = start + T * cycles;

    while (millis() < end) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1)
                servo_[i].Refresh();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Zeri::Execute(int A[], int O[], int T,
                   double P[], float steps)
{
    int cycles = (int)steps;

    for (int i = 0; i < cycles; i++)
        OscillateServos(A, O, T, P, 1.0);

    float remain = steps - cycles;
    if (remain > 0.01f)
        OscillateServos(A, O, T, P, remain);
}
