#if !defined(APPLICATION_H)
#define APPLICATION_H

#include "../contexts/task_sensor_context.h"
#include "../contexts/task_wifi_context.h"

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

    static int16_t pushDataToSupabase(void *pvParam, uint16_t *payload)
    {
        TaskWiFiContext *ctx = static_cast<TaskWiFiContext *>(pvParam);

        ctx->wifi.connect(ctx->ssid, ctx->password);
        while (!ctx->wifi.isConnected())
            k_msleep(50);
        const char *localIP = ctx->wifi.localIP();

        static char buf[128];
        snprintf(
            buf, sizeof(buf),
            "{\"fish_count_0\":%u,\"fish_count_1\":%u,\"fish_count_2\":%u,\"fish_count_3\":%u}",
            payload[0], payload[1], payload[2], payload[3]);

        int res = ctx->supa.post(ctx->table, buf);
        return res;
    }
};

#endif // APPLICATION_H
