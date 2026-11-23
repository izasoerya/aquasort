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

    static bool beginInternet(void *pvParam)
    {
        TaskWiFiContext *ctx = static_cast<TaskWiFiContext *>(pvParam);

        if (ctx->wifi.isConnected() && ctx->wifi.localIP() != nullptr)
            return true; // fast path: already online

        int ret = ctx->wifi.connect(ctx->ssid, ctx->password, K_SECONDS(15));
        if (ret != 0)
        {
            printk("Wi-Fi connect failed: %d\n", ret);
            return false;
        }

        const char *ip = ctx->wifi.localIP();
        printk("Device IP: %s\n", ip ? ip : "(none)");
        return ip != nullptr;
    }

    static int16_t pushDataToSupabase(void *pvParam, uint16_t *payload)
    {
        TaskWiFiContext *ctx = static_cast<TaskWiFiContext *>(pvParam);

        static char buf[128];
        snprintf(
            buf, sizeof(buf),
            "{\"fish_count_0\":%u,\"fish_count_1\":%u,\"fish_count_2\":%u,\"fish_count_3\":%u}",
            payload[0], payload[1], payload[2], payload[3]);
        if (beginInternet(ctx))
        {
            int res = ctx->supa.post(ctx->table, buf);
            return res;
        }
        return -ENOTCONN;
    }
};

#endif // APPLICATION_H
