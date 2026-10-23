#pragma once

#include <functional>
#include <memory>

#include "display/lcd_display.h"

/**
 * Cara del Osito: ojos procedurales dibujados con LVGL en la pantalla redonda.
 *
 * Sustituye los emojis de LcdDisplay por dos ojos en forma de píldora con brillo,
 * párpados, rubor y una boca pequeña. Parpadea, mueve la mirada en reposo, abre y
 * cierra la boca mientras habla y traduce las emociones del LLM a expresiones.
 * Conserva la barra de estado y los subtítulos de la clase base.
 */
class OsitoFaceDisplay : public SpiLcdDisplay {
public:
    enum class Expression {
        kNeutral,
        kHappy,
        kLove,
        kSad,
        kAngry,
        kSurprised,
        kThinking,
        kSleepy,
        kWink,
    };

    OsitoFaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy);
    virtual ~OsitoFaceDisplay();

    virtual void SetupUI() override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;

    // Muestra una expresión durante duration_ms y luego vuelve a la anterior.
    void ShowExpression(Expression expression, int duration_ms);
    // Se llama (fuera del bloqueo de LVGL) cada vez que cambia la expresión de base.
    void OnExpressionChanged(std::function<void(Expression)> callback);

    static Expression ExpressionFromName(const char* name);

private:
    enum class Mode { kIdle, kListening, kSpeaking };

    // Parámetros continuos de la cara; se interpolan cuadro a cuadro.
    struct FaceParams {
        float eye_w;
        float eye_h;
        float gaze_x;
        float gaze_y;
        float lid_top;      // 0..1 del ojo tapado por el párpado superior
        float lid_angle;    // grados; + enojado (cae hacia el centro), − triste
        float smile_left;   // 0..1 párpado inferior: ojo feliz "^"
        float smile_right;
        float blush;        // 0..1 opacidad del rubor
        float mouth;        // −1 triste … 0 recta … 1 sonrisa
        float mouth_open;   // 0..1
    };

    struct Eye {
        lv_obj_t* ball = nullptr;
        lv_obj_t* shine = nullptr;
        lv_obj_t* lid_top = nullptr;
        lv_obj_t* lid_bottom = nullptr;
        lv_obj_t* blush = nullptr;
    };

    static FaceParams ParamsFor(Expression expression);
    static void TimerCallback(lv_timer_t* timer);
    void Tick();
    void UpdateGaze(int64_t now_ms);
    void UpdateBlink(int64_t now_ms);
    void ApplyParams();
    void SetBaseExpression(Expression expression);

    lv_obj_t* face_ = nullptr;
    Eye eyes_[2];
    lv_obj_t* mouth_arc_ = nullptr;
    lv_obj_t* mouth_line_ = nullptr;
    lv_obj_t* mouth_open_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    FaceParams current_ = {};
    FaceParams target_ = {};
    Expression base_expression_ = Expression::kNeutral;
    Expression shown_expression_ = Expression::kNeutral;
    int64_t temporary_until_ms_ = 0;
    Mode mode_ = Mode::kIdle;

    float blink_ = 0.0f;  // 0 abierto, 1 cerrado
    int64_t next_blink_ms_ = 0;
    int64_t blink_start_ms_ = -1;
    int64_t next_gaze_ms_ = 0;
    float idle_gaze_x_ = 0.0f;
    float idle_gaze_y_ = 0.0f;
    int64_t next_mouth_ms_ = 0;
    float talk_open_ = 0.0f;

    std::function<void(Expression)> on_expression_changed_;
};
