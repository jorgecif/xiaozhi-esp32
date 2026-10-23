#pragma once

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <string>
#include <vector>

/**
 * Movimiento de la cabeza del Osito con dos micro-servos (giro y cabeceo).
 *
 * Ángulos en grados relativos al centro:
 *   pan  + = gira hacia su izquierda,  − = hacia su derecha
 *   tilt + = mira abajo,               − = mira arriba
 * Las trayectorias usan suavizado (smoothstep) y, al terminar, se corta el PWM para
 * que los servos no zumben ni gasten batería.
 */
class NuboMotion {
public:
    struct Limits {
        int pan_min;
        int pan_max;
        int tilt_min;
        int tilt_max;
    };

    NuboMotion(gpio_num_t pan_pin, gpio_num_t tilt_pin, const Limits& limits);
    ~NuboMotion();

    void Start();
    // Lleva la cabeza a una postura de reposo nueva; cancela lo que esté en curso.
    void MoveTo(int pan, int tilt, int duration_ms);
    // Gesto relativo a la postura de reposo. Devuelve false si el nombre no existe.
    bool PlayGesture(const std::string& name);
    // Corrección del centro de un eje (grados); se guarda en NVS.
    void SetTrim(bool pan_axis, int trim_deg);
    bool IsMoving() const { return moving_.load(); }

    static const char* GestureNames();

private:
    struct Keyframe {
        int16_t pan;
        int16_t tilt;
        int16_t duration_ms;
    };
    static constexpr int kMaxFrames = 12;
    struct Command {
        bool replace;
        bool relative;
        uint8_t count;
        Keyframe frames[kMaxFrames];
    };

    static void TaskEntry(void* arg);
    void Run();
    void Write(float pan, float tilt);
    void Detach();
    uint32_t AngleToDuty(float angle, bool invert, int trim) const;
    bool Send(const Command& command);

    gpio_num_t pan_pin_;
    gpio_num_t tilt_pin_;
    Limits limits_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> moving_{false};
    std::atomic<int> pan_trim_{0};
    std::atomic<int> tilt_trim_{0};
    bool attached_ = false;
};
