#include "components/ota_service/ota_service.h"
#include "services/main_application.h"
#include "task_sensor_context.h"
#include <cerrno>
#include <zephyr/sys/reboot.h>

int main()
{
    TaskSensorContext sensorCtx;
    TaskWiFiContext inetCtx;

    if (!MainApplication::beginInternet(&inetCtx))
        sys_reboot(SYS_REBOOT_COLD); // Assume it's Hang
    OTAService otaService;
    if (otaService.start(8080) == 0)
        OTAService::confirmImage();

    static uint16_t intCounter[4]{};
    std::function<void()> cbs[4] = {
        []() { intCounter[0]++; },
        []() { intCounter[1]++; },
        []() { intCounter[2]++; },
        []() { intCounter[3]++; },
    };

    MainApplication::initSensors(&sensorCtx, cbs);
    MainApplication::initButtons(&sensorCtx);
    for (uint8_t i = 0; i < 4; i++)
        sensorCtx.sensors[i].disableInterrupt();
    if (sensorCtx.pump.beginOutput() != 0)
        printk("Pump GPIO init failed\n");
    sensorCtx.pump.set(false);

    bool isInterruptEnabled = false;
    bool prevPressed = false;
    uint64_t lastPressMs = 0;
    const uint64_t buttonDebounceMs = 50;

    while (1)
    {
        const bool pressed = MainApplication::readButtons(&sensorCtx)[0];

        if (pressed && !prevPressed && k_uptime_get() - lastPressMs >= buttonDebounceMs)
        {
            lastPressMs = k_uptime_get();
            isInterruptEnabled = !isInterruptEnabled;
            printk("Button Sampling Pressed: %s\n", isInterruptEnabled ? "start" : "stop");

            if (isInterruptEnabled)
                for (uint8_t i = 0; i < 4; i++)
                {
                    sensorCtx.pump.set(true);
                    sensorCtx.sensors[i].enableInterrupt();
                }
            else
            {
                for (uint8_t i = 0; i < 4; i++)
                {
                    sensorCtx.pump.set(false);
                    sensorCtx.sensors[i].disableInterrupt();
                }
                int16_t res = MainApplication::pushDataToSupabase(&inetCtx, intCounter);
                printk("Supabase push result: %d\n", res);
                if (res == -ENOTCONN)
                {
                    printk("WiFi is not connected\n");
                    sys_reboot(SYS_REBOOT_COLD); // Assume it's Hang
                }
                memset(intCounter, 0, sizeof(intCounter));
            }
        }
        prevPressed = pressed;

        k_msleep(10);
    }
}