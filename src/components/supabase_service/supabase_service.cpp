#include "supabase_service.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

SupabaseService::SupabaseService(const char *project_url,
                                 const char *publishable_key,
                                 const char *ca_cert)
    : _ca_cert(ca_cert)
{
    _base_url[0] = '\0';
    _publishable_key[0] = '\0';
    _api_key_header[0] = '\0';
    _authorization_header[0] = '\0';
    _prefer_header[0] = '\0';

    if (project_url != nullptr)
    {
        strncpy(_base_url, project_url, sizeof(_base_url) - 1);
        _base_url[sizeof(_base_url) - 1] = '\0';
    }

    if (publishable_key != nullptr)
    {
        strncpy(_publishable_key, publishable_key, sizeof(_publishable_key) - 1);
        _publishable_key[sizeof(_publishable_key) - 1] = '\0';
    }

    snprintf(_api_key_header, sizeof(_api_key_header),
             "apikey: %s\r\n", _publishable_key);
    snprintf(_authorization_header, sizeof(_authorization_header),
             "Authorization: Bearer %s\r\n", _publishable_key);

    strcpy(_prefer_header, "Prefer: return=minimal\r\n");

    _header_fields[0] = _api_key_header;
    _header_fields[1] = _authorization_header;
    _header_fields[2] = _prefer_header;
    _header_fields[3] = nullptr;
}

int SupabaseService::post(const char *table_name, const char *payload)
{
    if (table_name == nullptr || payload == nullptr ||
        _base_url[0] == '\0' || _publishable_key[0] == '\0')
    {
        return -EINVAL;
    }

    char url[512];
    size_t base_length = strlen(_base_url);
    bool trailing_slash = base_length > 0 && _base_url[base_length - 1] == '/';

    int written = snprintf(url, sizeof(url), "%s%srest/v1/%s",
                           _base_url,
                           trailing_slash ? "" : "/",
                           table_name);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(url))
    {
        return -EMSGSIZE;
    }

    return postSecureWithHeaders(url, payload, _ca_cert, _header_fields);
}
