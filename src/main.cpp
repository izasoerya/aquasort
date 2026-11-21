#include "services/main_application.h"
#include "task_sensor_context.h"

int main()
{
    TaskSensorContext sensorCtx;

    static uint16_t intCounter[4]{};
    static uint64_t lastTriggerMs[4]{};
    const uint64_t debounceMs = 30;
    std::function<void()> cbs[4] = {
        []()
        {
            const uint64_t now = k_uptime_get();
            if (now - lastTriggerMs[0] >= debounceMs)
            {
                lastTriggerMs[0] = now;
                intCounter[0]++;
            }
        },
        []()
        {
            const uint64_t now = k_uptime_get();
            if (now - lastTriggerMs[1] >= debounceMs)
            {
                lastTriggerMs[1] = now;
                intCounter[1]++;
            }
        },
        []()
        {
            const uint64_t now = k_uptime_get();
            if (now - lastTriggerMs[2] >= debounceMs)
            {
                lastTriggerMs[2] = now;
                intCounter[2]++;
            }
        },
        []()
        {
            const uint64_t now = k_uptime_get();
            if (now - lastTriggerMs[3] >= debounceMs)
            {
                lastTriggerMs[3] = now;
                intCounter[3]++;
            }
        },
    };

    MainApplication::initSensors(&sensorCtx, cbs);
    MainApplication::initButtons(&sensorCtx);

    while (1)
    {
        bool *ptrStateButtons = MainApplication::readButtons(&sensorCtx);
        bool stateButtons[2] = {ptrStateButtons[0], ptrStateButtons[1]};
        static bool isInterruptEnabled = stateButtons[0];

        static uint64_t prev = k_uptime_get();
        if (k_uptime_get() - prev >= 1000 && stateButtons[0])
        {
            prev = k_uptime_get();

            isInterruptEnabled = !isInterruptEnabled;
            if (isInterruptEnabled)
                for (uint8_t i = 0; i < 4; i++)
                    sensorCtx.sensors[i].enableInterrupt();
            else
            {
                for (uint8_t i = 0; i < 4; i++)
                    sensorCtx.sensors[i].disableInterrupt();
                memset(intCounter, 0, sizeof(intCounter));
            }
        }

        k_yield();
    }
}