#include "digital_sensor_component.h"

DigitalSensorComponent::DigitalSensorComponent(const gpio_dt_spec *pin) : _pin(pin)
{
}

const int8_t DigitalSensorComponent::begin()
{
    if (gpio_is_ready_dt(_pin))
        return gpio_pin_configure_dt(_pin, GPIO_INPUT);
    return false;
}

const int8_t DigitalSensorComponent::beginOutput()
{
    if (!gpio_is_ready_dt(_pin))
        return -ENODEV;
    return gpio_pin_configure_dt(_pin, GPIO_OUTPUT_INACTIVE);
}

const int8_t DigitalSensorComponent::beginInterrupt(std::function<void()> cb)
{
    int result = begin();
    if (result == 0)
    {
        _onTriggered = cb;
        _cbCtx.owner = this;
        gpio_init_callback(&_cbCtx.cb, isrHandler, BIT(_pin->pin));
        gpio_add_callback(_pin->port, &_cbCtx.cb);
        return 0;
    }
    return result;
}

int8_t DigitalSensorComponent::enableInterrupt(gpio_flags_t mode)
{
    return gpio_pin_interrupt_configure_dt(_pin, mode);
}

int8_t DigitalSensorComponent::disableInterrupt()
{
    return gpio_pin_interrupt_configure_dt(_pin, GPIO_INT_DISABLE);
}

const int8_t DigitalSensorComponent::set(bool state)
{
    return gpio_pin_set_dt(_pin, state);
}

const bool DigitalSensorComponent::state()
{
    return gpio_pin_get_dt(_pin);
}
