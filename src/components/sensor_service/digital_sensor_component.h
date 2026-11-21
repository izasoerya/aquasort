#if !defined(DIGITAL_SENSOR_COMPONENT_H)
#define DIGITAL_SENSOR_COMPONENT_H

#include <functional>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

class DigitalSensorComponent
{
private:
    struct GpioCbCtx
    {
        struct gpio_callback cb;
        DigitalSensorComponent *owner;
    };

    const gpio_dt_spec *_pin;
    GpioCbCtx _cbCtx;
    std::function<void()> _onTriggered;

    static void isrHandler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
    {
        auto *ctx = reinterpret_cast<GpioCbCtx *>(cb);
        if (ctx->owner->_onTriggered)
            ctx->owner->_onTriggered();
    }

public:
    DigitalSensorComponent(const gpio_dt_spec *pin);

    const int8_t begin();
    const int8_t beginInterrupt(std::function<void()> cb);
    int8_t enableInterrupt();
    int8_t disableInterrupt();
    const bool state();
};

#endif // DIGITAL_SENSOR_COMPONENT_H
