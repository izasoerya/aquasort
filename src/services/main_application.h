#if !defined(APPLICATION_H)
#define APPLICATION_H

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include "../contexts/task_sensor_context.h"

class MainApplication
{
public:
    static bool *initSensors(void *pvParam, std::function<void()> *cbInt)
    {
        TaskSensorContext *ctx = static_cast<TaskSensorContext *>(pvParam);

        static bool successInitSensors[4]{};
        for (uint8_t i = 0; i < 4; i++)
            successInitSensors[i] = ctx->sensors[i].beginInterrupt([i, cbInt]() { cbInt[i](); });
        return successInitSensors;
    }

    static bool *initButtons(void *pvParam)
    {
        TaskSensorContext *ctx = static_cast<TaskSensorContext *>(pvParam);

        static bool successInitButtons[2]{};
        for (uint8_t i = 0; i < 2; i++)
            successInitButtons[i] = ctx->buttons[i].begin();
        return successInitButtons;
    }

    static bool *readButtons(void *pvParam)
    {
        TaskSensorContext *ctx = static_cast<TaskSensorContext *>(pvParam);

        static bool stateButton[2]{};
        stateButton[0] = ctx->buttons[0].state();
        stateButton[1] = ctx->buttons[1].state();

        return stateButton;
    }
};

#endif // APPLICATION_H
