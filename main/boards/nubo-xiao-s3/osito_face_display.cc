#include "osito_face_display.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "assets/lang_config.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "OsitoFace"

namespace {

constexpr int kFrameMs = 33;      // ~30 fps
constexpr int kEyeSpacing = 38;   // centro de cada ojo respecto al centro de la pantalla
constexpr int kBlinkMs = 160;
constexpr int kBlushW = 34;
constexpr int kBlushH = 16;
constexpr int kBlushDx = 62;

lv_color_t EyeColor() { return lv_color_hex(0x8FF3FF); }
lv_color_t BlushColor() { return lv_color_hex(0xFF8FB1); }

int64_t NowMs() { return esp_timer_get_time() / 1000; }

float RandomRange(float low, float high) {
    return low + (high - low) * static_cast<float>(esp_random() % 1000) / 1000.0f;
}

void Approach(float& value, float goal, float k) { value += (goal - value) * k; }

lv_obj_t* MakeShape(lv_obj_t* parent, lv_color_t color, int32_t radius) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

struct EmotionAlias {
    const char* name;
    OsitoFaceDisplay::Expression expression;
};

// Emociones que envía el servidor (campo "emotion") e íconos de alertas del firmware
constexpr EmotionAlias kEmotionAliases[] = {
    {"neutral", OsitoFaceDisplay::Expression::kNeutral},
    {"relaxed", OsitoFaceDisplay::Expression::kNeutral},
    {"happy", OsitoFaceDisplay::Expression::kHappy},
    {"laughing", OsitoFaceDisplay::Expression::kHappy},
    {"funny", OsitoFaceDisplay::Expression::kHappy},
    {"delicious", OsitoFaceDisplay::Expression::kHappy},
    {"confident", OsitoFaceDisplay::Expression::kHappy},
    {"cool", OsitoFaceDisplay::Expression::kHappy},
    {"loving", OsitoFaceDisplay::Expression::kLove},
    {"kissy", OsitoFaceDisplay::Expression::kLove},
    {"embarrassed", OsitoFaceDisplay::Expression::kLove},
    {"sad", OsitoFaceDisplay::Expression::kSad},
    {"crying", OsitoFaceDisplay::Expression::kSad},
    {"angry", OsitoFaceDisplay::Expression::kAngry},
    {"surprised", OsitoFaceDisplay::Expression::kSurprised},
    {"shocked", OsitoFaceDisplay::Expression::kSurprised},
    {"thinking", OsitoFaceDisplay::Expression::kThinking},
    {"confused", OsitoFaceDisplay::Expression::kThinking},
    {"sleepy", OsitoFaceDisplay::Expression::kSleepy},
    {"winking", OsitoFaceDisplay::Expression::kWink},
    {"silly", OsitoFaceDisplay::Expression::kWink},
    {"triangle_exclamation", OsitoFaceDisplay::Expression::kSad},
    {"cloud_slash", OsitoFaceDisplay::Expression::kSad},
    {"cancel", OsitoFaceDisplay::Expression::kSad},
    // nombres cortos que usan las herramientas MCP de la placa
    {"love", OsitoFaceDisplay::Expression::kLove},
    {"wink", OsitoFaceDisplay::Expression::kWink},
};

}  // namespace

OsitoFaceDisplay::OsitoFaceDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel, int width, int height,
                                   int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                   bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {
    current_ = target_ = ParamsFor(Expression::kNeutral);
}

OsitoFaceDisplay::~OsitoFaceDisplay() {
    DisplayLockGuard lock(this);
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (face_ != nullptr) {
        lv_obj_delete(face_);
        face_ = nullptr;
    }
}

OsitoFaceDisplay::Expression OsitoFaceDisplay::ExpressionFromName(const char* name) {
    if (name == nullptr) {
        return Expression::kNeutral;
    }
    for (const auto& alias : kEmotionAliases) {
        if (strcmp(alias.name, name) == 0) {
            return alias.expression;
        }
    }
    return Expression::kNeutral;
}

OsitoFaceDisplay::FaceParams OsitoFaceDisplay::ParamsFor(Expression expression) {
    FaceParams p = {};
    p.eye_w = 44;
    p.eye_h = 62;
    p.blush = 0.5f;  // con menos opacidad el rosa se ve café sobre el negro
    p.mouth = 0.5f;
    switch (expression) {
        case Expression::kNeutral:
            break;
        case Expression::kHappy:
            p.eye_w = 46;
            p.eye_h = 58;
            p.smile_left = p.smile_right = 1.0f;
            p.blush = 0.8f;
            p.mouth = 1.0f;
            break;
        case Expression::kLove:
            p.eye_w = 48;
            p.smile_left = p.smile_right = 1.0f;
            p.blush = 1.0f;
            p.mouth = 1.0f;
            break;
        case Expression::kSad:
            p.eye_w = 42;
            p.eye_h = 56;
            p.lid_top = 0.35f;
            p.lid_angle = -18;
            p.gaze_y = 8;
            p.blush = 0.0f;
            p.mouth = -0.8f;
            break;
        case Expression::kAngry:
            p.lid_top = 0.38f;
            p.lid_angle = 22;
            p.blush = 0.0f;
            p.mouth = -0.5f;
            break;
        case Expression::kSurprised:
            p.eye_w = 50;
            p.eye_h = 70;
            p.blush = 0.2f;
            p.mouth = 0.0f;
            p.mouth_open = 0.8f;
            break;
        case Expression::kThinking:
            p.eye_w = 42;
            p.eye_h = 56;
            p.gaze_x = -12;
            p.gaze_y = -12;
            p.lid_top = 0.15f;
            p.mouth = 0.0f;
            break;
        case Expression::kSleepy:
            p.lid_top = 0.62f;
            p.lid_angle = -6;
            p.gaze_y = 6;
            p.blush = 0.3f;
            p.mouth = 0.0f;
            break;
        case Expression::kWink:
            p.smile_left = 1.0f;
            p.blush = 0.6f;
            p.mouth = 0.9f;
            break;
    }
    return p;
}

void OsitoFaceDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    // La clase base crea la barra de estado, las notificaciones y los subtítulos
    SpiLcdDisplay::SetupUI();

    auto* dark_theme = LvglThemeManager::GetInstance().GetTheme("dark");
    if (dark_theme != nullptr) {
        SetTheme(dark_theme);
    }

    DisplayLockGuard lock(this);
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }

    auto screen = lv_screen_active();
    face_ = lv_obj_create(screen);
    lv_obj_remove_style_all(face_);
    lv_obj_set_size(face_, width_, height_);
    lv_obj_set_pos(face_, 0, 0);
    lv_obj_remove_flag(face_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(face_, LV_OBJ_FLAG_CLICKABLE);
    // Justo encima del fondo y debajo de barras, vista previa y subtítulos
    lv_obj_move_to_index(face_, 1);

    // El orden de creación define qué se dibuja encima
    for (auto& eye : eyes_) {
        eye.ball = MakeShape(face_, EyeColor(), LV_RADIUS_CIRCLE);
        eye.shine = MakeShape(eye.ball, lv_color_white(), LV_RADIUS_CIRCLE);
    }
    for (auto& eye : eyes_) {
        eye.lid_top = MakeShape(face_, lv_color_black(), 0);
        eye.lid_bottom = MakeShape(face_, lv_color_black(), LV_RADIUS_CIRCLE);
    }
    for (auto& eye : eyes_) {
        eye.blush = MakeShape(face_, BlushColor(), LV_RADIUS_CIRCLE);
    }

    mouth_arc_ = lv_arc_create(face_);
    lv_obj_remove_style_all(mouth_arc_);
    lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(mouth_arc_, EyeColor(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(mouth_arc_, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(mouth_arc_, true, LV_PART_MAIN);
    mouth_line_ = MakeShape(face_, EyeColor(), LV_RADIUS_CIRCLE);
    mouth_open_ = MakeShape(face_, EyeColor(), LV_RADIUS_CIRCLE);

    const int64_t now = NowMs();
    next_blink_ms_ = now + 1500;
    next_gaze_ms_ = now + 2000;
    ApplyParams();
    timer_ = lv_timer_create(TimerCallback, kFrameMs, this);
}

void OsitoFaceDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);
    // La cara siempre va sobre negro: parece un visor de vidrio
    DisplayLockGuard lock(this);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    if (container_ != nullptr) {
        lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    }
}

void OsitoFaceDisplay::SetEmotion(const char* emotion) {
    SetBaseExpression(ExpressionFromName(emotion));
}

void OsitoFaceDisplay::SetBaseExpression(Expression expression) {
    bool changed = false;
    {
        DisplayLockGuard lock(this);
        changed = expression != base_expression_;
        base_expression_ = expression;
        if (temporary_until_ms_ == 0) {
            shown_expression_ = expression;
            target_ = ParamsFor(expression);
        }
    }
    if (changed && on_expression_changed_) {
        on_expression_changed_(expression);
    }
}

void OsitoFaceDisplay::ShowExpression(Expression expression, int duration_ms) {
    DisplayLockGuard lock(this);
    shown_expression_ = expression;
    target_ = ParamsFor(expression);
    temporary_until_ms_ = NowMs() + std::max(duration_ms, 1);
}

void OsitoFaceDisplay::OnExpressionChanged(std::function<void(Expression)> callback) {
    on_expression_changed_ = std::move(callback);
}

void OsitoFaceDisplay::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);
    if (status == nullptr) {
        return;
    }
    Mode mode = Mode::kIdle;
    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        mode = Mode::kListening;
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        mode = Mode::kSpeaking;
    }
    DisplayLockGuard lock(this);
    mode_ = mode;
    if (mode != Mode::kIdle) {
        // Al escuchar o hablar, mira de frente
        idle_gaze_x_ = 0;
        idle_gaze_y_ = 0;
    }
}

void OsitoFaceDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    const bool showing = image != nullptr;
    SpiLcdDisplay::SetPreviewImage(std::move(image));
    DisplayLockGuard lock(this);
    // La clase base vuelve a mostrar el emoji al quitar la vista previa
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (face_ != nullptr) {
        if (showing) {
            lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(face_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void OsitoFaceDisplay::TimerCallback(lv_timer_t* timer) {
    // Corre dentro de la tarea de LVGL, que ya tiene el bloqueo
    auto* self = static_cast<OsitoFaceDisplay*>(lv_timer_get_user_data(timer));
    self->Tick();
}

void OsitoFaceDisplay::UpdateGaze(int64_t now_ms) {
    if (mode_ != Mode::kIdle || shown_expression_ != Expression::kNeutral) {
        idle_gaze_x_ = 0;
        idle_gaze_y_ = 0;
        return;
    }
    if (now_ms >= next_gaze_ms_) {
        if (esp_random() % 3 == 0) {
            idle_gaze_x_ = 0;
            idle_gaze_y_ = 0;
        } else {
            idle_gaze_x_ = RandomRange(-14, 14);
            idle_gaze_y_ = RandomRange(-8, 6);
        }
        next_gaze_ms_ = now_ms + 1200 + esp_random() % 3000;
    }
}

void OsitoFaceDisplay::UpdateBlink(int64_t now_ms) {
    if (blink_start_ms_ < 0 && now_ms >= next_blink_ms_) {
        blink_start_ms_ = now_ms;
    }
    if (blink_start_ms_ < 0) {
        return;
    }
    const int64_t t = now_ms - blink_start_ms_;
    if (t >= kBlinkMs) {
        blink_ = 0;
        blink_start_ms_ = -1;
        // A veces parpadea dos veces seguidas
        next_blink_ms_ = now_ms + (esp_random() % 6 == 0 ? 220 : 2200 + esp_random() % 3800);
        return;
    }
    const float x = static_cast<float>(t) / kBlinkMs;
    blink_ = x < 0.5f ? x * 2.0f : (1.0f - x) * 2.0f;
}

void OsitoFaceDisplay::Tick() {
    const int64_t now = NowMs();
    if (temporary_until_ms_ != 0 && now >= temporary_until_ms_) {
        temporary_until_ms_ = 0;
        shown_expression_ = base_expression_;
        target_ = ParamsFor(shown_expression_);
    }

    UpdateGaze(now);
    UpdateBlink(now);

    FaceParams goal = target_;
    goal.gaze_x += idle_gaze_x_;
    goal.gaze_y += idle_gaze_y_;
    if (mode_ == Mode::kSpeaking) {
        if (now >= next_mouth_ms_) {
            talk_open_ = RandomRange(0.15f, 0.85f);
            next_mouth_ms_ = now + 80 + esp_random() % 90;
        }
        goal.mouth_open = std::max(goal.mouth_open, talk_open_);
    }

    Approach(current_.eye_w, goal.eye_w, 0.3f);
    Approach(current_.eye_h, goal.eye_h, 0.3f);
    Approach(current_.gaze_x, goal.gaze_x, 0.45f);
    Approach(current_.gaze_y, goal.gaze_y, 0.45f);
    Approach(current_.lid_top, goal.lid_top, 0.25f);
    Approach(current_.lid_angle, goal.lid_angle, 0.25f);
    Approach(current_.smile_left, goal.smile_left, 0.3f);
    Approach(current_.smile_right, goal.smile_right, 0.3f);
    Approach(current_.blush, goal.blush, 0.15f);
    Approach(current_.mouth, goal.mouth, 0.3f);
    Approach(current_.mouth_open, goal.mouth_open, 0.5f);
    ApplyParams();
}

void OsitoFaceDisplay::ApplyParams() {
    if (face_ == nullptr) {
        return;
    }
    const FaceParams& p = current_;
    const int cx = width_ / 2;
    const int eye_y = height_ * 45 / 100;
    const float scale = mode_ == Mode::kListening ? 1.08f : 1.0f;  // atento al escuchar
    const float w = p.eye_w * scale;
    const float h = std::max(4.0f, p.eye_h * scale * (1.0f - 0.92f * blink_));

    for (int i = 0; i < 2; i++) {
        const int side = i == 0 ? -1 : 1;  // 0 = ojo de la izquierda de la pantalla
        const float ex = cx + side * kEyeSpacing + p.gaze_x;
        const float ey = eye_y + p.gaze_y;
        Eye& eye = eyes_[i];

        lv_obj_set_pos(eye.ball, std::lround(ex - w / 2), std::lround(ey - h / 2));
        lv_obj_set_size(eye.ball, std::lround(w), std::lround(h));

        const int shine = std::lround(w * 0.3f);
        if (h > shine + 8) {
            lv_obj_remove_flag(eye.shine, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(eye.shine, shine, shine);
            lv_obj_set_pos(eye.shine, std::lround(w * 0.58f), std::lround(h * 0.14f));
        } else {
            lv_obj_add_flag(eye.shine, LV_OBJ_FLAG_HIDDEN);
        }

        // Párpado superior: rectángulo negro que baja e inclina hacia el centro (enojo)
        // o hacia afuera (tristeza). Rotación positiva = sentido horario.
        const int lid_w = std::lround(w + 26);
        const int lid_h = std::lround(p.eye_h + 20);
        const float lid_edge = ey - h / 2 + p.lid_top * h;
        lv_obj_set_size(eye.lid_top, lid_w, lid_h);
        lv_obj_set_pos(eye.lid_top, std::lround(ex - lid_w / 2.0f), std::lround(lid_edge - lid_h));
        lv_obj_set_style_transform_pivot_x(eye.lid_top, lid_w / 2, 0);
        lv_obj_set_style_transform_pivot_y(eye.lid_top, lid_h, 0);
        lv_obj_set_style_transform_rotation(eye.lid_top, std::lround(-side * p.lid_angle * 10),
                                            0);

        // Párpado inferior: elipse negra que sube y deja el ojo en forma de "^"
        const float smile = i == 0 ? p.smile_left : p.smile_right;
        const int bottom_w = std::lround(w * 1.5f);
        const int bottom_h = std::lround(h * 1.1f);
        const float bottom_top = (ey + h / 2 + 2) - smile * (h * 0.65f + 2);
        lv_obj_set_size(eye.lid_bottom, bottom_w, bottom_h);
        lv_obj_set_pos(eye.lid_bottom, std::lround(ex - bottom_w / 2.0f), std::lround(bottom_top));

        lv_obj_set_size(eye.blush, kBlushW, kBlushH);
        lv_obj_set_pos(eye.blush, cx + side * kBlushDx - kBlushW / 2, eye_y + 40 - kBlushH / 2);
        lv_obj_set_style_bg_opa(eye.blush, static_cast<lv_opa_t>(p.blush * 200), 0);
    }

    // Boca: abierta (hablando o sorpresa), sonrisa, puchero o línea
    const int mouth_y = eye_y + 52;
    if (p.mouth_open > 0.12f) {
        const int mw = std::lround(12 + 10 * p.mouth_open);
        const int mh = std::lround(4 + 16 * p.mouth_open);
        lv_obj_set_size(mouth_open_, mw, mh);
        lv_obj_set_pos(mouth_open_, cx - mw / 2, mouth_y - mh / 2 + 2);
        lv_obj_remove_flag(mouth_open_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
    } else if (std::fabs(p.mouth) > 0.2f) {
        constexpr int kArcSize = 34;
        const float amount = std::min(1.0f, std::fabs(p.mouth));
        const int span = std::lround(40 + 80 * amount);
        lv_obj_set_size(mouth_arc_, kArcSize, kArcSize);
        if (p.mouth > 0) {
            // Mitad inferior del círculo = sonrisa
            lv_obj_set_pos(mouth_arc_, cx - kArcSize / 2, mouth_y - kArcSize + 4);
            lv_arc_set_bg_angles(mouth_arc_, 90 - span / 2, 90 + span / 2);
        } else {
            lv_obj_set_pos(mouth_arc_, cx - kArcSize / 2, mouth_y - 2);
            lv_arc_set_bg_angles(mouth_arc_, 270 - span / 2, 270 + span / 2);
        }
        lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_open_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_size(mouth_line_, 16, 5);
        lv_obj_set_pos(mouth_line_, cx - 8, mouth_y);
        lv_obj_remove_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_open_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
    }
}
