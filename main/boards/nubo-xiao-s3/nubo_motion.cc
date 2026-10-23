#include "nubo_motion.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstring>

#include "config.h"
#include "settings.h"

#define TAG "NuboMotion"

namespace {

constexpr int kStepMs = 20;           // 50 Hz, igual que el PWM de los servos
constexpr int kDetachDelayMs = 400;   // tras el último movimiento se corta el PWM
constexpr int kPeriodUs = 20000;
constexpr ledc_timer_t kTimer = LEDC_TIMER_1;  // TIMER_0/CH0 lo usa la retroiluminación
constexpr ledc_channel_t kPanChannel = LEDC_CHANNEL_1;
constexpr ledc_channel_t kTiltChannel = LEDC_CHANNEL_2;
constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_14_BIT;

}  // namespace

NuboMotion::NuboMotion(gpio_num_t pan_pin, gpio_num_t tilt_pin, const Limits& limits)
    : pan_pin_(pan_pin), tilt_pin_(tilt_pin), limits_(limits) {}

NuboMotion::~NuboMotion() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
    if (queue_ != nullptr) {
        vQueueDelete(queue_);
    }
}

const char* NuboMotion::GestureNames() {
    return "nod (asentir), shake (negar), curious (curioso), happy (contento), sad (triste), "
           "surprised (sorpresa), think (pensar), hello (saludar), dance (bailar), "
           "sleep (dormir), center (centro)";
}

void NuboMotion::Start() {
    Settings settings("nubo", false);
    pan_trim_ = settings.GetInt("pan_trim", 0);
    tilt_trim_ = settings.GetInt("tilt_trim", 0);

    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                 .duty_resolution = kResolution,
                                 .timer_num = kTimer,
                                 .freq_hz = 50,
                                 .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const gpio_num_t pins[] = {pan_pin_, tilt_pin_};
    const ledc_channel_t channels[] = {kPanChannel, kTiltChannel};
    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t channel = {.gpio_num = pins[i],
                                         .speed_mode = LEDC_LOW_SPEED_MODE,
                                         .channel = channels[i],
                                         .intr_type = LEDC_INTR_DISABLE,
                                         .timer_sel = kTimer,
                                         .duty = 0,
                                         .hpoint = 0};
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
    }

    queue_ = xQueueCreate(4, sizeof(Command));
    xTaskCreate(TaskEntry, "nubo_motion", 3072, this, 2, &task_);
    MoveTo(0, 0, 600);
    ESP_LOGI(TAG, "Servos listos (trim giro %d, cabeceo %d)", pan_trim_.load(), tilt_trim_.load());
}

uint32_t NuboMotion::AngleToDuty(float angle, bool invert, int trim) const {
    float servo_deg = 90.0f + (invert ? -angle : angle) + trim;
    servo_deg = std::clamp(servo_deg, 0.0f, 180.0f);
    const float pulse_us =
        SERVO_MIN_PULSE_US + (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * servo_deg / 180.0f;
    return static_cast<uint32_t>(pulse_us * ((1 << kResolution) - 1) / kPeriodUs);
}

void NuboMotion::Write(float pan, float tilt) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kPanChannel, AngleToDuty(pan, SERVO_PAN_INVERT, pan_trim_));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kPanChannel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kTiltChannel,
                  AngleToDuty(tilt, SERVO_TILT_INVERT, tilt_trim_));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kTiltChannel);
    attached_ = true;
}

void NuboMotion::Detach() {
    // Sin pulsos el servo queda suelto: no zumba ni consume
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kPanChannel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kPanChannel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kTiltChannel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kTiltChannel);
    attached_ = false;
}

bool NuboMotion::Send(const Command& command) {
    if (queue_ == nullptr) {
        return false;
    }
    if (xQueueSend(queue_, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Cola de movimientos llena");
        return false;
    }
    return true;
}

void NuboMotion::MoveTo(int pan, int tilt, int duration_ms) {
    Command command = {};
    command.replace = true;
    command.relative = false;
    command.count = 1;
    command.frames[0] = {static_cast<int16_t>(pan), static_cast<int16_t>(tilt),
                         static_cast<int16_t>(std::clamp(duration_ms, 0, 5000))};
    Send(command);
}

bool NuboMotion::PlayGesture(const std::string& name) {
    struct Gesture {
        const char* name;
        const char* alias;
        uint8_t count;
        Keyframe frames[kMaxFrames];
    };
    // Posturas relativas a la de reposo; todas terminan en (0, 0)
    static const Gesture kGestures[] = {
        {"nod", "asentir", 4, {{0, 10, 180}, {0, -4, 160}, {0, 10, 160}, {0, 0, 200}}},
        {"shake", "negar", 4, {{-18, 0, 180}, {18, 0, 260}, {-18, 0, 260}, {0, 0, 200}}},
        {"curious", "curioso", 3, {{14, -12, 350}, {14, -12, 600}, {0, 0, 400}}},
        {"happy",
         "contento",
         5,
         {{-8, -4, 120}, {8, -4, 140}, {-8, -4, 140}, {8, -4, 140}, {0, 0, 160}}},
        {"sad", "triste", 3, {{0, 12, 700}, {0, 12, 1200}, {0, 0, 700}}},
        {"surprised", "sorpresa", 3, {{0, -16, 120}, {0, -16, 500}, {0, 0, 400}}},
        {"think", "pensar", 3, {{-14, -12, 500}, {-14, -12, 800}, {0, 0, 500}}},
        {"hello", "saludar", 4, {{10, -8, 200}, {-10, -8, 300}, {10, -8, 300}, {0, 0, 250}}},
        {"dance",
         "bailar",
         7,
         {{-25, -6, 300},
          {25, 6, 400},
          {-25, -6, 400},
          {25, 6, 400},
          {-15, -10, 300},
          {15, -10, 300},
          {0, 0, 300}}},
    };

    if (name == "sleep" || name == "dormir") {
        MoveTo(0, limits_.tilt_max, 1200);
        return true;
    }
    if (name == "center" || name == "centro") {
        MoveTo(0, 0, 500);
        return true;
    }
    for (const auto& gesture : kGestures) {
        if (name == gesture.name || name == gesture.alias) {
            Command command = {};
            command.replace = true;
            command.relative = true;
            command.count = gesture.count;
            memcpy(command.frames, gesture.frames, sizeof(command.frames));
            return Send(command);
        }
    }
    return false;
}

void NuboMotion::SetTrim(bool pan_axis, int trim_deg) {
    trim_deg = std::clamp(trim_deg, -20, 20);
    Settings settings("nubo", true);
    if (pan_axis) {
        pan_trim_ = trim_deg;
        settings.SetInt("pan_trim", trim_deg);
    } else {
        tilt_trim_ = trim_deg;
        settings.SetInt("tilt_trim", trim_deg);
    }
    MoveTo(0, 0, 300);
}

void NuboMotion::TaskEntry(void* arg) { static_cast<NuboMotion*>(arg)->Run(); }

void NuboMotion::Run() {
    std::vector<Keyframe> pending;
    Keyframe active = {};
    bool has_active = false;
    float cur_pan = 0;
    float cur_tilt = 0;
    float from_pan = 0;
    float from_tilt = 0;
    int64_t start_ms = 0;
    int64_t idle_since_ms = 0;
    int rest_pan = 0;
    int rest_tilt = 0;

    for (;;) {
        Command command;
        TickType_t wait = (has_active || !pending.empty()) ? 0 : pdMS_TO_TICKS(100);
        while (xQueueReceive(queue_, &command, wait) == pdTRUE) {
            wait = 0;
            if (command.replace) {
                pending.clear();
                has_active = false;
            }
            for (int i = 0; i < command.count && i < kMaxFrames; i++) {
                Keyframe frame = command.frames[i];
                if (command.relative) {
                    frame.pan += rest_pan;
                    frame.tilt += rest_tilt;
                }
                frame.pan = static_cast<int16_t>(
                    std::clamp<int>(frame.pan, limits_.pan_min, limits_.pan_max));
                frame.tilt = static_cast<int16_t>(
                    std::clamp<int>(frame.tilt, limits_.tilt_min, limits_.tilt_max));
                pending.push_back(frame);
            }
            if (!command.relative && command.count > 0) {
                rest_pan = pending.back().pan;
                rest_tilt = pending.back().tilt;
            }
        }

        const int64_t now = esp_timer_get_time() / 1000;
        if (!has_active && !pending.empty()) {
            active = pending.front();
            pending.erase(pending.begin());
            from_pan = cur_pan;
            from_tilt = cur_tilt;
            start_ms = now;
            has_active = true;
        }

        if (has_active) {
            float t = active.duration_ms > 0
                          ? static_cast<float>(now - start_ms) / active.duration_ms
                          : 1.0f;
            if (t >= 1.0f) {
                t = 1.0f;
                has_active = false;
                idle_since_ms = now;
            }
            const float s = t * t * (3.0f - 2.0f * t);
            cur_pan = from_pan + (active.pan - from_pan) * s;
            cur_tilt = from_tilt + (active.tilt - from_tilt) * s;
            Write(cur_pan, cur_tilt);
        } else if (attached_ && pending.empty() && now - idle_since_ms > kDetachDelayMs) {
            Detach();
        }

        moving_.store(has_active || !pending.empty());
        vTaskDelay(pdMS_TO_TICKS(kStepMs));
    }
}
