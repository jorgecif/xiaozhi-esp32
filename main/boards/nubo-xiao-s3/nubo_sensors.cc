#include "nubo_sensors.h"

#include <esp_log.h>

#include <algorithm>

#define TAG "NuboSensors"

namespace {

constexpr int kTimeoutMs = 50;

// MAX17048
constexpr uint8_t kGaugeSoc = 0x04;
constexpr uint8_t kGaugeVersion = 0x08;
constexpr uint8_t kGaugeCrate = 0x16;  // %/h con signo, 0.208 %/h por LSB

// LSM6DS3TR-C / LSM6DSL
constexpr uint8_t kImuWhoAmI = 0x0F;
constexpr uint8_t kImuWhoAmIValue = 0x6A;
constexpr uint8_t kImuTapSrc = 0x1C;
constexpr uint8_t kTapIa = 0x40;
constexpr uint8_t kSingleTap = 0x20;
constexpr uint8_t kDoubleTap = 0x10;

}  // namespace

NuboI2cSensor::~NuboI2cSensor() {
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
    }
}

bool NuboI2cSensor::Attach(i2c_master_bus_handle_t bus, uint8_t addr) {
    if (i2c_master_probe(bus, addr, kTimeoutMs) != ESP_OK) {
        return false;
    }
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags =
            {
                .disable_ack_check = 0,
            },
    };
    return i2c_master_bus_add_device(bus, &config, &dev_) == ESP_OK;
}

bool NuboI2cSensor::ReadRegs(uint8_t reg, uint8_t* data, size_t length) {
    return i2c_master_transmit_receive(dev_, &reg, 1, data, length, kTimeoutMs) == ESP_OK;
}

bool NuboI2cSensor::WriteReg(uint8_t reg, uint8_t value) {
    const uint8_t buffer[2] = {reg, value};
    return i2c_master_transmit(dev_, buffer, sizeof(buffer), kTimeoutMs) == ESP_OK;
}

FuelGauge* FuelGauge::Create(i2c_master_bus_handle_t bus, uint8_t addr) {
    auto* gauge = new FuelGauge();
    uint8_t version[2] = {};
    if (!gauge->Attach(bus, addr) || !gauge->ReadRegs(kGaugeVersion, version, sizeof(version))) {
        ESP_LOGW(TAG, "MAX17048 no encontrado en 0x%02x", addr);
        delete gauge;
        return nullptr;
    }
    ESP_LOGI(TAG, "MAX17048 versión 0x%02x%02x", version[0], version[1]);
    return gauge;
}

bool FuelGauge::Read(int& percent, bool& charging) {
    uint8_t soc[2] = {};
    uint8_t crate[2] = {};
    if (!ReadRegs(kGaugeSoc, soc, sizeof(soc)) || !ReadRegs(kGaugeCrate, crate, sizeof(crate))) {
        return false;
    }
    percent = std::clamp<int>(soc[0], 0, 100);
    const int16_t rate = static_cast<int16_t>((crate[0] << 8) | crate[1]);
    charging = rate > 5;  // sube más de ~1 %/h
    return true;
}

TapSensor* TapSensor::Create(i2c_master_bus_handle_t bus, uint8_t addr) {
    auto* sensor = new TapSensor();
    uint8_t who_am_i = 0;
    if (!sensor->Attach(bus, addr) || !sensor->ReadRegs(kImuWhoAmI, &who_am_i, 1) ||
        who_am_i != kImuWhoAmIValue) {
        ESP_LOGW(TAG, "IMU LSM6DS3TR-C no encontrado en 0x%02x (WHO_AM_I 0x%02x)", addr, who_am_i);
        delete sensor;
        return nullptr;
    }
    // Toque simple y doble según la nota de aplicación ST AN5130
    const uint8_t init[][2] = {
        {0x10, 0x60},  // CTRL1_XL: acelerómetro a 416 Hz, ±2 g
        {0x58, 0x8F},  // TAP_CFG: interrupciones, toque en X/Y/Z, enclavado hasta leer TAP_SRC
        {0x59, 0x8C},  // TAP_THS_6D: umbral del toque
        {0x5A, 0x7F},  // INT_DUR2: duración, silencio y ventana del doble toque
        {0x5B, 0x80},  // WAKE_UP_THS: habilita toque simple y doble
    };
    for (const auto& reg : init) {
        if (!sensor->WriteReg(reg[0], reg[1])) {
            ESP_LOGW(TAG, "No se pudo configurar el IMU");
            delete sensor;
            return nullptr;
        }
    }
    ESP_LOGI(TAG, "IMU listo: toque = caricia, doble toque = hablar");
    return sensor;
}

TapSensor::Tap TapSensor::Poll() {
    uint8_t source = 0;
    if (!ReadRegs(kImuTapSrc, &source, 1) || (source & kTapIa) == 0) {
        return Tap::kNone;
    }
    if (source & kDoubleTap) {
        return Tap::kDouble;
    }
    if (source & kSingleTap) {
        return Tap::kSingle;
    }
    return Tap::kNone;
}
