#pragma once

#include <driver/i2c_master.h>

#include <cstddef>
#include <cstdint>

// Sensores opcionales del bus I2C. Si no responden, Create() devuelve nullptr y la
// placa sigue funcionando sin ellos (útil en el banco de pruebas).
class NuboI2cSensor {
public:
    virtual ~NuboI2cSensor();

protected:
    i2c_master_dev_handle_t dev_ = nullptr;

    bool Attach(i2c_master_bus_handle_t bus, uint8_t addr);
    bool ReadRegs(uint8_t reg, uint8_t* data, size_t length);
    bool WriteReg(uint8_t reg, uint8_t value);
};

// Medidor de batería MAX17048: porcentaje real y si se está cargando.
class FuelGauge : public NuboI2cSensor {
public:
    static FuelGauge* Create(i2c_master_bus_handle_t bus, uint8_t addr);
    bool Read(int& percent, bool& charging);
};

// Toques sobre la cabeza con el IMU LSM6DS3TR-C (o LSM6DSL).
class TapSensor : public NuboI2cSensor {
public:
    enum class Tap { kNone, kSingle, kDouble };

    static TapSensor* Create(i2c_master_bus_handle_t bus, uint8_t addr);
    Tap Poll();
};
