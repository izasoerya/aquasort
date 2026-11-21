#if !defined(TASK_SENSOR_CONTEXT_H)
#define TASK_SENSOR_CONTEXT_H

#include "../components/sensor_service/digital_sensor_component.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

struct TaskSensorContext
{
    inline static constexpr struct gpio_dt_spec sensorSpec[4] = {
        GPIO_DT_SPEC_GET(DT_NODELABEL(ir_0), gpios),
        GPIO_DT_SPEC_GET(DT_NODELABEL(ir_1), gpios),
        GPIO_DT_SPEC_GET(DT_NODELABEL(ir_2), gpios),
        GPIO_DT_SPEC_GET(DT_NODELABEL(ir_3), gpios),
    };
    DigitalSensorComponent sensors[4];

    inline static constexpr struct gpio_dt_spec buttonSpec[2] = {
        GPIO_DT_SPEC_GET(DT_NODELABEL(start_button), gpios),
        GPIO_DT_SPEC_GET(DT_NODELABEL(reset_button), gpios),
    };
    DigitalSensorComponent buttons[2];

    TaskSensorContext()
        : sensors{
              DigitalSensorComponent(&sensorSpec[0]),
              DigitalSensorComponent(&sensorSpec[1]),
              DigitalSensorComponent(&sensorSpec[2]),
              DigitalSensorComponent(&sensorSpec[3]),
          },
          buttons{
              DigitalSensorComponent(&buttonSpec[0]),
              DigitalSensorComponent(&buttonSpec[1]),
          }
    {
    }
};

#endif // TASK_SENSOR_CONTEXT_H
