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

        constexpr uint8_t kMaxAttempts = 5;
        WiFiService *candidates[] = {&ctx->ugmWiFi, &ctx->basicWiFi};

        // Both services share one radio, so both see the same connect/disconnect
        // events. Reuse the link that is up instead of re-running the sequence
        // (which would disconnect a working basic link to retry UGM on every push).
        static WiFiService *active = nullptr;
        if (active != nullptr && active->isConnected())
            return true;
        active = nullptr;

        for (WiFiService *wifi : candidates)
        {
            if (wifi != candidates[0])
                wifi->disconnect();

            for (uint8_t attempt = 1; attempt <= kMaxAttempts; attempt++)
            {
                int16_t ret = -ENOTCONN;
                if (wifi == &ctx->ugmWiFi)
                    ret = wifi->connect(ctx->usernameUGM, ctx->passwordUGM, K_SECONDS(15));
                else if (wifi == &ctx->basicWiFi)
                    ret = wifi->connect(ctx->ssid, ctx->password, K_SECONDS(15));
                if (ret == 0)
                {
                    printk("[WiFi] Connected via %s, Device IP: %s\n",
                           wifi == &ctx->ugmWiFi ? "UGM" : "basic", wifi->localIP());
                    active = wifi;
                    return true;
                }
                printk("[WiFi] %s attempt %u/%u failed: %d\n",
                       wifi == &ctx->ugmWiFi ? "UGM" : "basic", attempt, kMaxAttempts, ret);
                k_sleep(K_SECONDS(5));
            }
        }
        printk("[WiFi] All connection attempts failed\n");
        return false;
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
            constexpr uint8_t kMaxAttempts = 5;
            int res = 0;
            for (uint8_t attempt = 1; attempt <= kMaxAttempts; attempt++)
            {
                res = ctx->supa.post(ctx->table, buf);
                if (res >= 200 && res < 300)
                {
                    printk("[Supa] Post success (%d)\n", res);
                    return res;
                }
                if (res < 0)
                {
                    printk("[Supa] Post failed (%d), forcing reconnect\n", res);
                    ctx->basicWiFi.disconnect();
                    return res;
                }

                bool retryable = res == 408 || res == 429 || res >= 500;
                if (!retryable)
                {
                    printk("[Supa] Post rejected (%d), not retrying\n", res);
                    return res;
                }
                printk("[Supa] Post failed (%d), retry %u/%u\n", res, attempt, kMaxAttempts);
                k_msleep(500 * attempt); // simple linear backoff
            }
            return res;
        }
        return -ENOTCONN;
    }
};

#endif // APPLICATION_H
