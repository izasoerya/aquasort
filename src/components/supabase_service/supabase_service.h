#pragma once

#include "../wifi_service/wifi_service.h"

class SupabaseService : public WiFiService
{
private:
    char _base_url[256];
    char _publishable_key[256];
    const char *_ca_cert;
    char _api_key_header[320];
    char _authorization_header[320];
    char _prefer_header[32];
    const char *_header_fields[4];

public:
    SupabaseService(const char *project_url,
                    const char *publishable_key,
                    const char *ca_cert = nullptr);

    int post(const char *table_name, const char *payload) override;
};
