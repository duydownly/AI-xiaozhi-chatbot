#include "puppy_movements.h"

#include <algorithm>

#include "freertos/idf_additions.h"
#include "oscillator.h"

static const char* TAG = "PuppyMovements";

Puppy::Puppy() {
    is_puppy_resting_ = false;

    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_pins_[i] = -1;
        servo_trim_[i] = 0;
    }
}

Puppy::~Puppy() {
    DetachServos();
}

unsigned long IRAM_ATTR millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

void Puppy::Init(int left_leg, int right_leg, int left_foot, int right_foot, int tail_servo) {
    servo_pins_[LEFT_LEG] = left_leg;
    servo_pins_[RIGHT_LEG] = right_leg;
    servo_pins_[LEFT_FOOT] = left_foot;
    servo_pins_[RIGHT_FOOT] = right_foot;
    servo_pins_[TAIL] = tail_servo;

    AttachServos();
    is_puppy_resting_ = false;
}

///////////////////////////////////////////////////////////////////
//-- ATTACH & DETACH FUNCTIONS ----------------------------------//
///////////////////////////////////////////////////////////////////
void Puppy::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Attach(servo_pins_[i]);
        }
    }
}

void Puppy::DetachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Detach();
        }
    }
}

///////////////////////////////////////////////////////////////////
//-- OSCILLATORS TRIMS ------------------------------------------//
///////////////////////////////////////////////////////////////////
void Puppy::SetTrims(int left_leg, int right_leg, int left_foot, int right_foot, int tail_trim) {
    servo_trim_[LEFT_LEG] = left_leg;
    servo_trim_[RIGHT_LEG] = right_leg;
    servo_trim_[LEFT_FOOT] = left_foot;
    servo_trim_[RIGHT_FOOT] = right_foot;
    servo_trim_[TAIL] = tail_trim;

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetTrim(servo_trim_[i]);
        }
    }
}

///////////////////////////////////////////////////////////////////
//-- BASIC MOTION FUNCTIONS -------------------------------------//
///////////////////////////////////////////////////////////////////
void Puppy::MoveServos(int time, int servo_target[]) {
    if (GetRestState())
        SetRestState(false);

    final_time_ = millis() + time;
    if (time > 10) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                increment_[i] =
                    (servo_target[i] - servo_[i].GetPosition()) / (time / 10.0);
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
}

void Puppy::MoveSingle(int position, int servo_number) {
    if (position > 180) position = 180;
    if (position < 0) position = 0;

    if (servo_number >= 0 && servo_number < SERVO_COUNT &&
        servo_pins_[servo_number] != -1) {
        servo_[servo_number].SetPosition(position);
    }
}

void Puppy::OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                            int period, double phase_diff[SERVO_COUNT], float cycle) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetO(offset[i]);
            servo_[i].SetA(amplitude[i]);
            servo_[i].SetT(period);
            servo_[i].SetPh(phase_diff[i]);
        }
    }

    unsigned long ref = millis();
    unsigned long end_time = period * cycle + ref;

    while (millis() < end_time) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1)
                servo_[i].Refresh();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Puppy::Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                    int period, double phase_diff[SERVO_COUNT], float steps) {
    int cycles = (int)steps;

    for (int i = 0; i < cycles; i++)
        OscillateServos(amplitude, offset, period, phase_diff);

    OscillateServos(amplitude, offset, period, phase_diff, steps - cycles);
}

void Puppy::Execute2(int amplitude[SERVO_COUNT], int center_angle[SERVO_COUNT],
                     int period, double phase_diff[SERVO_COUNT], float steps) {

    int offset[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++)
        offset[i] = center_angle[i] - 90;

    Execute(amplitude, offset, period, phase_diff, steps);
}

///////////////////////////////////////////////////////////////////
//-- HOME POSITION -----------------------------------------------//
///////////////////////////////////////////////////////////////////
void Puppy::Home() {
    int homes[SERVO_COUNT] = {90, 90, 90, 90, 90};
    MoveServos(500, homes);
    is_puppy_resting_ = true;
}

bool Puppy::GetRestState() { return is_puppy_resting_; }
void Puppy::SetRestState(bool state) { is_puppy_resting_ = state; }

///////////////////////////////////////////////////////////////////
//-- MOVEMENT FUNCTIONS ------------------------------------------//
///////////////////////////////////////////////////////////////////
void Puppy::Walk(float steps, int period, int dir) {
    int A[SERVO_COUNT] = {30, 30, 25, 25, 0};
    int O[SERVO_COUNT] = {0, 0, 5, -5, 0};
    double phase[SERVO_COUNT] = {
        0,
        0,
        DEG2RAD(dir * -90),
        DEG2RAD(dir * -90),
        0
    };

    Execute(A, O, period, phase, steps);
}

void Puppy::Turn(float steps, int period, int dir) {
    int A[SERVO_COUNT] = {30, 30, 25, 25, 0};
    int O[SERVO_COUNT] = {0, 0, 5, -5, 0};
    double phase[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(-90), 0};

    if (dir == LEFT) {
        A[LEFT_LEG] = 30;
        A[RIGHT_LEG] = 5;
    } else {
        A[LEFT_LEG] = 5;
        A[RIGHT_LEG] = 30;
    }

    Execute(A, O, period, phase, steps);
}

void Puppy::Sit() {
    int pos[SERVO_COUNT] = {120, 60, 20, 160, 90};
    MoveServos(800, pos);
}

void Puppy::Jump(float steps, int period) {
    int up[SERVO_COUNT] = {90, 90, 150, 30, 90};
    int down[SERVO_COUNT] = {90, 90, 90, 90, 90};

    MoveServos(period, up);
    MoveServos(period, down);
}

void Puppy::Swing(float steps, int period, int height) {
    int A[SERVO_COUNT] = {0, 0, height, height, 0};
    int O[SERVO_COUNT] = {0, 0, height / 2, -height / 2, 0};
    double ph[SERVO_COUNT] = {0, 0, 0, 0, 0};

    Execute(A, O, period, ph, steps);
}

void Puppy::UpDown(float steps, int period, int height) {
    int A[SERVO_COUNT] = {0, 0, height, height, 0};
    int O[SERVO_COUNT] = {0, 0, height, -height, 0};
    double ph[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(90), 0};

    Execute(A, O, period, ph, steps);
}

void Puppy::Moonwalker(float steps, int period, int height, int dir) {
    int A[SERVO_COUNT] = {0, 0, height, height, 0};
    int O[SERVO_COUNT] = {0, 0, height / 2, -height / 2, 0};
    double ph[SERVO_COUNT] = {0, 0, DEG2RAD(dir * -90),
                              DEG2RAD(-60 * dir), 0};

    Execute(A, O, period, ph, steps);
}

///////////////////////////////////////////////////////////////////
//-- TAIL ACTIONS ------------------------------------------------//
///////////////////////////////////////////////////////////////////
void Puppy::TailWag(int times, int speed) {
    for (int i = 0; i < times; i++) {
        MoveSingle(60, TAIL);
        vTaskDelay(pdMS_TO_TICKS(speed));
        MoveSingle(120, TAIL);
        vTaskDelay(pdMS_TO_TICKS(speed));
    }
}

void Puppy::TailHappy() {
    TailWag(6, 150);
}

void Puppy::TailSad() {
    MoveSingle(70, TAIL);
}

///////////////////////////////////////////////////////////////////
//-- LIMIT -------------------------------------------------------//
///////////////////////////////////////////////////////////////////
void Puppy::EnableServoLimit(int limit) {
    for (int i = 0; i < SERVO_COUNT; i++)
        if (servo_pins_[i] != -1)
            servo_[i].SetLimiter(limit);
}

void Puppy::DisableServoLimit() {
    for (int i = 0; i < SERVO_COUNT; i++)
        if (servo_pins_[i] != -1)
            servo_[i].DisableLimiter();
}
