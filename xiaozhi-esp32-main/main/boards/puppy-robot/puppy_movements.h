#ifndef __PUPPY_MOVEMENTS_H__
#define __PUPPY_MOVEMENTS_H__

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oscillator.h"

/* =================================
   Direction & motion constants
   ================================= */
#define FORWARD   1
#define BACKWARD -1
#define LEFT      1
#define RIGHT    -1

#define SMALL     5
#define MEDIUM   15
#define BIG      30

#define SERVO_LIMIT_DEFAULT 240

/* =================================
   Servo index mapping (5 SERVO)
   ================================= */
#define LEFT_LEG     0
#define RIGHT_LEG    1
#define LEFT_FOOT    2
#define RIGHT_FOOT   3
#define TAIL         4

#define SERVO_COUNT 5   // chỉ 5 servo: 4 chân + 1 đuôi

class Puppy {
public:
    Puppy();
    ~Puppy();

    /* Initialization */
    void Init(int left_leg, int right_leg, int left_foot, int right_foot, int tail_servo);

    /* Servo attach/detach */
    void AttachServos();
    void DetachServos();

    /* Servo trims */
    void SetTrims(int left_leg, int right_leg, int left_foot, int right_foot, int tail_trim = 0);

    /* Base movement functions */
    void MoveServos(int time, int servo_target[]);
    void MoveSingle(int position, int servo_number);

    void OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                         double phase_diff[SERVO_COUNT], float cycle);
    void Execute2(int amplitude[SERVO_COUNT], int center_angle[SERVO_COUNT], int period,
                  double phase_diff[SERVO_COUNT], float steps);
    void Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                 double phase_diff[SERVO_COUNT], float steps);

    /* Home position */
    void Home();

    bool GetRestState();
    void SetRestState(bool state);

    /* Movements for walking robot */
    void Walk(float steps = 4, int period = 1000, int dir = FORWARD);
    void Turn(float steps = 4, int period = 1200, int dir = LEFT);
    void Sit();
    void Jump(float steps = 1, int period = 1500);

    void Swing(float steps = 1, int period = 900, int height = 20);
    void UpDown(float steps = 1, int period = 900, int height = 20);
    void Moonwalker(float steps = 1, int period = 900, int height = 20, int dir = LEFT);

    /* NEW : Tail movements */
    void TailWag(int times = 4, int speed = 300);
    void TailHappy();
    void TailSad();

    /* Servo limiter */
    void EnableServoLimit(int speed_limit_degree_per_sec = SERVO_LIMIT_DEFAULT);
    void DisableServoLimit();

private:
    Oscillator servo_[SERVO_COUNT];

    int servo_pins_[SERVO_COUNT];
    int servo_trim_[SERVO_COUNT];

    unsigned long final_time_;
    unsigned long partial_time_;
    float increment_[SERVO_COUNT];

    bool is_puppy_resting_;
};

#endif // __PUPPY_MOVEMENTS_H__
