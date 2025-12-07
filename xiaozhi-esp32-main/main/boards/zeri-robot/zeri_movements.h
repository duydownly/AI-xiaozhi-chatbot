#ifndef __ZERI_MOVEMENTS_H__
#define __ZERI_MOVEMENTS_H__

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oscillator.h"

//------------------------------------------------------
// Constants
//------------------------------------------------------
#define FORWARD   1
#define BACKWARD -1
#define LEFT      1
#define RIGHT    -1

#define SMALL   5
#define MEDIUM 15
#define BIG    30

#define SERVO_LIMIT_DEFAULT 240   // degree/sec limit

//------------------------------------------------------
// Servo indexes (5 servo robot: 4 legs + tail)
//------------------------------------------------------
#define FRONT_LEFT_LEG     0
#define FRONT_RIGHT_LEG    1
#define BACK_LEFT_LEG      2
#define BACK_RIGHT_LEG     3
#define TAIL_SERVO         4

#define SERVO_COUNT        5

//------------------------------------------------------
//  Zeri class definition
//------------------------------------------------------
class Zeri {
public:
    Zeri();
    ~Zeri();

    // Initialization (5 servo only)
    void Init(int front_left_leg,
              int front_right_leg,
              int back_left_leg,
              int back_right_leg,
              int tail_servo);

    // Attach / Detach
    void AttachServos();
    void DetachServos();

    // Trim calibration
    void SetTrims(int front_left_trim,
                  int front_right_trim,
                  int back_left_trim,
                  int back_right_trim,
                  int tail_trim);

    // Movements Core
    void MoveServos(int time, int servo_target[]);
    void OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                         int period, double phase_diff[SERVO_COUNT], float cycles);

    void Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                 int period, double phase_diff[SERVO_COUNT], float steps);

    // Basic APIs
    void Home();
    bool GetRestState();
    void SetRestState(bool state);

    // Motion presets (supported for 4 legs + tail)
    void Walk(float steps = 4, int period = 1000, int direction = FORWARD);
    void Turn(float steps = 4, int period = 900, int direction = LEFT);
    void Swing(float steps = 1, int period = 1000, int height = 20);
    void UpDown(float steps = 1, int period = 900, int height = 20);

    void ShakeTail(float steps = 2, int period = 600, int amplitude = 35);

    void Sit();

    // Servo limiter
    void EnableServoLimit(int speed_limit_degree_per_sec = SERVO_LIMIT_DEFAULT);
    void DisableServoLimit();

private:
    Oscillator servo_[SERVO_COUNT];

    int servo_pins_[SERVO_COUNT];
    int servo_trim_[SERVO_COUNT];
    float increment_[SERVO_COUNT];

    unsigned long final_time_;
    unsigned long partial_time_;

    bool is_zeri_resting_;
};

#endif  // __ZERI_MOVEMENTS_H__
