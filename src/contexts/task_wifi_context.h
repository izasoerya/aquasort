#include "../components/supabase_service/supabase_service.h"
#include "../components/wifi_service/wifi_service.h"

struct TaskWiFiContext
{
    const char *ssid = "4G-UFI-CCDs";
    const char *password = "1234567890";

    const char *projectUrl = "https://gothabjdasaphwzrjnto.supabase.co";
    const char *publishKey = "sb_publishable_Dx3vXSh8qdQhM1Zi_V1MTQ_ifqdbX1o";
    const char *table = "Aquasort_sensor";

    WiFiService wifi;
    SupabaseService supa;

    TaskWiFiContext() : supa(SupabaseService(projectUrl, publishKey))
    {
    }
};