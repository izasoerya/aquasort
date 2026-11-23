#if !defined(TASK_SENSOR_CONTEXT_H)
#define TASK_SENSOR_CONTEXT_H

#include "../components/sensor_service/digital_sensor_component.h"
#include "../components/sensor_service/pass_counter_component.h"
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
    PassCounterComponent sensors[4];

    inline static constexpr struct gpio_dt_spec buttonSpec[2] = {
        GPIO_DT_SPEC_GET(DT_NODELABEL(start_button), gpios),
        GPIO_DT_SPEC_GET(DT_NODELABEL(reset_button), gpios),
    };
    DigitalSensorComponent buttons[2];

    inline static constexpr struct gpio_dt_spec pumpSpec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(pump_0), gpios);
    DigitalSensorComponent pump;

    TaskSensorContext()
        : sensors{
              PassCounterComponent(&sensorSpec[0]),
              PassCounterComponent(&sensorSpec[1]),
              PassCounterComponent(&sensorSpec[2]),
              PassCounterComponent(&sensorSpec[3]),
          },
          buttons{
              DigitalSensorComponent(&buttonSpec[0]),
              DigitalSensorComponent(&buttonSpec[1]),
          },
          pump{
              DigitalSensorComponent(&pumpSpec),
          }
    {
    }
};

#endif // TASK_SENSOR_CONTEXT_H
