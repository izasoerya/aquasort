#include "../components/supabase_service/supabase_service.h"
#include "../components/wifi_service/internet_ugm.h"
#include "../components/wifi_service/wifi_service.h"

struct TaskWiFiContext
{
    const char *ssid = "ihza.surya0302";
    const char *password = "Makanmieayam4kali";
    const char *hostname = "aquasort_device";

    const char *projectUrl = "https://gothabjdasaphwzrjnto.supabase.co";
    const char *publishKey = "sb_publishable_Dx3vXSh8qdQhM1Zi_V1MTQ_ifqdbX1o";
    const char *table = "Aquasort_sensor";

    InternetUGM wifi;
    SupabaseService supa;

    TaskWiFiContext() : supa(SupabaseService(projectUrl, publishKey))
    {
        wifi.setHostname(hostname);
    }
};