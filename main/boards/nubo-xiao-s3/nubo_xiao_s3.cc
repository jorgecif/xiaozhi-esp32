#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_gc9a01.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "esp32_camera.h"
#include "mcp_server.h"
#include "nubo_motion.h"
#include "nubo_sensors.h"
#include "osito_face_display.h"
#include "wifi_board.h"

#define TAG "NuboXiaoS3"

using Expression = OsitoFaceDisplay::Expression;

class NuboXiaoS3 : public WifiBoard {
private:
    Button boot_button_;
    NuboMotion motion_;
    OsitoFaceDisplay* display_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    i2c_master_bus_handle_t sensor_bus_ = nullptr;
    FuelGauge* fuel_gauge_ = nullptr;
    TapSensor* tap_sensor_ = nullptr;
    Expression last_expression_ = Expression::kNeutral;

    void InitializeSpi() {
        spi_bus_config_t buscfg =
            GC9A01_PANEL_BUS_SPI_CONFIG(DISPLAY_SPI_SCLK_PIN, DISPLAY_SPI_MOSI_PIN,
                                        DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t io_handle = nullptr;
        esp_lcd_panel_io_spi_config_t io_config =
            GC9A01_PANEL_IO_SPI_CONFIG(DISPLAY_SPI_CS_PIN, DISPLAY_SPI_DC_PIN, NULL, NULL);
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io_handle));

        esp_lcd_panel_handle_t panel_handle = nullptr;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

        display_ = new OsitoFaceDisplay(io_handle, panel_handle, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                        DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeCamera() {
        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        // TIMER_0/CH0 es de la retroiluminación y TIMER_1/CH1-2 de los servos
        config.ledc_timer = LEDC_TIMER_2;
        config.ledc_channel = LEDC_CHANNEL_3;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
    }

    void InitializeSensors() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = SENSOR_I2C_PORT,
            .sda_io_num = SENSOR_I2C_SDA_PIN,
            .scl_io_num = SENSOR_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &sensor_bus_));

        fuel_gauge_ = FuelGauge::Create(sensor_bus_, FUEL_GAUGE_I2C_ADDR);
        tap_sensor_ = TapSensor::Create(sensor_bus_, IMU_I2C_ADDR);
        if (tap_sensor_ != nullptr) {
            xTaskCreate(
                [](void* arg) { static_cast<NuboXiaoS3*>(arg)->PollTaps(); }, "nubo_taps", 3072,
                this, 2, nullptr);
        }
    }

    // Toque = caricia (se pone contento). Doble toque = empezar o terminar de hablar.
    void PollTaps() {
        int64_t pending_single_ms = 0;
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(60));
            const int64_t now = esp_timer_get_time() / 1000;
            const auto tap = tap_sensor_->Poll();
            if (motion_.IsMoving()) {
                // La vibración de los servos parece un toque
                continue;
            }
            if (tap == TapSensor::Tap::kDouble) {
                pending_single_ms = 0;
                Application::GetInstance().ToggleChatState();
            } else if (tap == TapSensor::Tap::kSingle && pending_single_ms == 0) {
                // Espera un poco por si es el primero de un doble toque
                pending_single_ms = now + 350;
            }
            if (pending_single_ms != 0 && now >= pending_single_ms) {
                pending_single_ms = 0;
                display_->ShowExpression(Expression::kHappy, 1500);
                motion_.PlayGesture("happy");
            }
        }
    }

    // Cada cambio de emoción en la cara se acompaña con un gesto de la cabeza
    void InitializeMotion() {
        motion_.Start();
        display_->OnExpressionChanged([this](Expression expression) {
            switch (expression) {
                case Expression::kHappy:
                case Expression::kLove:
                    motion_.PlayGesture("happy");
                    break;
                case Expression::kSad:
                    motion_.PlayGesture("sad");
                    break;
                case Expression::kAngry:
                    motion_.PlayGesture("shake");
                    break;
                case Expression::kSurprised:
                    motion_.PlayGesture("surprised");
                    break;
                case Expression::kThinking:
                    motion_.PlayGesture("think");
                    break;
                case Expression::kSleepy:
                    motion_.PlayGesture("sleep");
                    break;
                case Expression::kWink:
                    motion_.PlayGesture("curious");
                    break;
                case Expression::kNeutral:
                    // Solo "dormir" deja la cabeza fuera del centro
                    if (last_expression_ == Expression::kSleepy) {
                        motion_.PlayGesture("center");
                    }
                    break;
            }
            last_expression_ = expression;
        });
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.nubo.look",
            "Mueve la cabeza del robot a una postura y la mantiene. pan: giro en grados (-75 a "
            "75, positivo = hacia su izquierda). tilt: cabeceo en grados (-25 a 12, negativo = "
            "mirar arriba). duration_ms: duración del movimiento.",
            PropertyList({Property("pan", kPropertyTypeInteger, 0, HEAD_PAN_MIN_DEG,
                                   HEAD_PAN_MAX_DEG),
                          Property("tilt", kPropertyTypeInteger, 0, HEAD_TILT_MIN_DEG,
                                   HEAD_TILT_MAX_DEG),
                          Property("duration_ms", kPropertyTypeInteger, 500, 100, 3000)}),
            [this](const PropertyList& properties) -> ToolResult {
                motion_.MoveTo(properties["pan"].value<int>(), properties["tilt"].value<int>(),
                               properties["duration_ms"].value<int>());
                return true;
            });

        mcp_server.AddTool(
            "self.nubo.gesture",
            std::string("Hace un gesto con la cabeza. name: ") + NuboMotion::GestureNames(),
            PropertyList({Property("name", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ToolResult {
                const auto name = properties["name"].value<std::string>();
                if (!motion_.PlayGesture(name)) {
                    return std::unexpected("Gesto desconocido: " + name);
                }
                return true;
            });

        mcp_server.AddTool(
            "self.nubo.expression",
            "Muestra una expresión en la cara durante unos segundos. name: neutral, happy, love, "
            "sad, angry, surprised, thinking, sleepy, wink. seconds: duración.",
            PropertyList({Property("name", kPropertyTypeString),
                          Property("seconds", kPropertyTypeInteger, 3, 1, 30)}),
            [this](const PropertyList& properties) -> ToolResult {
                const auto name = properties["name"].value<std::string>();
                display_->ShowExpression(OsitoFaceDisplay::ExpressionFromName(name.c_str()),
                                         properties["seconds"].value<int>() * 1000);
                return true;
            });

        mcp_server.AddUserOnlyTool(
            "self.nubo.calibrate",
            "Ajusta el centro de un servo y lo guarda. axis: pan (giro) o tilt (cabeceo). trim: "
            "corrección en grados (-20 a 20).",
            PropertyList({Property("axis", kPropertyTypeString),
                          Property("trim", kPropertyTypeInteger, 0, -20, 20)}),
            [this](const PropertyList& properties) -> ToolResult {
                const auto axis = properties["axis"].value<std::string>();
                if (axis != "pan" && axis != "tilt") {
                    return std::unexpected("axis debe ser pan o tilt");
                }
                motion_.SetTrim(axis == "pan", properties["trim"].value<int>());
                return true;
            });
    }

public:
    NuboXiaoS3()
        : boot_button_(BOOT_BUTTON_GPIO),
          motion_(SERVO_PAN_PIN, SERVO_TILT_PIN,
                  {HEAD_PAN_MIN_DEG, HEAD_PAN_MAX_DEG, HEAD_TILT_MIN_DEG, HEAD_TILT_MAX_DEG}) {
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeSensors();
        InitializeCamera();
        InitializeMotion();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplexPdm audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override { return camera_; }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (fuel_gauge_ == nullptr || !fuel_gauge_->Read(level, charging)) {
            return false;
        }
        discharging = !charging;
        return true;
    }
};

DECLARE_BOARD(NuboXiaoS3);
