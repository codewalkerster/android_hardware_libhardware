/*
 *    Copyright (c) 2019 Sangchul Go <luke.go@hardkernel.com>
 *
 *    OdroidThings is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as
 *    published by the Free Software Foundation, either version 3 of the
 *    License, or (at your option) any later version.
 *
 *    OdroidThings is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with OdroidThings.
 *    If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ANDROID_INCLUDE_HARDWARE_ODROID_THINGS_H
#define ANDROID_INCLUDE_HARDWARE_ODROID_THINGS_H

#include <hardware/hardware.h>
#include <vector>
#include <string>
#include <unistd.h>

#include "odroidthings-base.h"

#define ODROID_THINGS_MODULE_API_VERSION_1_0 HARDWARE_MODULE_API_VERSION(1, 0)
#define ODROID_THINGS_HARDWARE_MODULE_ID    "odroidThings"

#define PIN_MAX 41
#define I2C_MAX 2
#define PWM_MAX 4
#define UART_MAX 1
#define SPI_MAX 1

namespace hardware {
namespace hardkernel {
namespace odroidthings {

typedef struct pin{
    std::string name;
    int pin;
    int availableModes;
} pin_t;

typedef struct i2c{
    std::string name;
    std::string path;
} i2c_t;

typedef struct pwm{
    int index;
    int chip;
    int line;
} pwm_t;

typedef struct uart{
    std::string name;
    std::string path;
} uart_t;

typedef struct spi {
    std::string name;
    std::string path;
} spi_t;

typedef void (*function_t)(void);

typedef struct common_operations {
    const std::vector<std::string> (*getPinNameList)();
    const std::vector<std::string> (*getListOf)(int);
} common_operations_t;

typedef struct gpio_operations {
    bool (*getValue)(int);
    void (*setDirection)(int, direction_t);
    void (*setValue)(int, bool);
    void (*setActiveType)(int, int);
    void (*setEdgeTriggerType)(int, int);
    void (*registerCallback)(int, function_t);
    void (*unregisterCallback)(int);
} gpio_operations_t;

typedef struct pwm_operations {
    void (*open)(int);
    void (*close)(int);
    bool (*setEnable)(int, bool);
    bool (*setDutyCycle)(int, double);
    bool (*setFrequency)(int, double);
} pwm_operations_t;

typedef struct i2c_operations {
    void (*open)(int, uint32_t, int);
    void (*close)(int);
    const std::vector<uint8_t> (*readRegBuffer)(int, uint32_t, int);
    Result (*writeRegBuffer)(int, uint32_t, std::vector<uint8_t>, int);
} i2c_operations_t;

typedef struct uart_operations {
    void (*open)(int);
    void (*close)(int);
    bool (*clearModemControl)(int, int);
    bool (*flush)(int, int);
    bool (*sendBreak)(int, int);
    bool (*isSupportBaudrate)(int, int);
    bool (*setBaudrate)(int, int);
    bool (*setDataSize)(int, int);
    bool (*setHardwareFlowControl)(int, int);
    bool (*setModemControl)(int, int);
    bool (*setParity)(int, int);
    bool (*setStopBits)(int, int);

    const std::vector<uint8_t> (*read)(int, int);
    ssize_t (*write)(int, std::vector<uint8_t>, int);

    void (*registerCallback)(int, function_t);
    void (*unregisterCallback)(int);
} uart_operations_t;

typedef struct spi_operations {
    void (*open)(int);
    void (*close)(int);
    bool (*setBitJustification)(int, uint8_t);
    bool (*setBitsPerWord)(int, uint8_t);
    bool (*setMode)(int, uint8_t);
    bool (*setCsChange)(int, bool);
    bool (*setDelay)(int, uint16_t);
    bool (*setFrequency)(int, uint32_t);

    const std::vector<uint8_t> (*transfer)(int, std::vector<uint8_t>, int);
    bool (*write)(int, std::vector<uint8_t>, int);
    const std::vector<uint8_t> (*read)(int, int);
} spi_operations_t;

typedef struct things_device {
    hw_device_t common;
    common_operations_t common_ops;
    gpio_operations_t gpio_ops;
    pwm_operations_t pwm_ops;
    i2c_operations_t i2c_ops;
    uart_operations_t uart_ops;
    spi_operations_t spi_ops;
} things_device_t;

typedef struct things_module {
    struct hw_module_t common;
    void (*init)();
    const std::vector<pin_t> (*getPinList)();
} things_module_t;

} // namespace odroidthings
} // namespace hardkernel
} // namespace hardware

#endif
