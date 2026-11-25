#include "internet_ugm.h"

#include <cerrno>
#include <cstdio>
#include <zephyr/sys/printk.h>

namespace
{
constexpr char ugm_ssid[] = "UGM-Hotspot";
constexpr char ugm_login_url[] =
    "https://sso.ugm.ac.id/cas/login?service=https://internet.ugm.ac.id/sso/login";

int appendText(char *output, size_t capacity, size_t &length, const char *text)
{
    while (*text != '\0')
    {
        if (length + 1 >= capacity)
        {
            return -EMSGSIZE;
        }
        output[length++] = *text++;
    }

    output[length] = '\0';
    return 0;
}

int appendJsonString(char *output, size_t capacity, size_t &length, const char *value)
{
    int ret = appendText(output, capacity, length, "\"");
    if (ret != 0)
    {
        return ret;
    }

    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char *character = reinterpret_cast<const unsigned char *>(value);
         *character != '\0'; ++character)
    {
        char escaped[6];
        size_t escaped_length = 1;

        switch (*character)
        {
        case '"':
        case '\\':
            escaped[0] = '\\';
            escaped[1] = static_cast<char>(*character);
            escaped_length = 2;
            break;
        case '\b':
            escaped[0] = '\\';
            escaped[1] = 'b';
            escaped_length = 2;
            break;
        case '\f':
            escaped[0] = '\\';
            escaped[1] = 'f';
            escaped_length = 2;
            break;
        case '\n':
            escaped[0] = '\\';
            escaped[1] = 'n';
            escaped_length = 2;
            break;
        case '\r':
            escaped[0] = '\\';
            escaped[1] = 'r';
            escaped_length = 2;
            break;
        case '\t':
            escaped[0] = '\\';
            escaped[1] = 't';
            escaped_length = 2;
            break;
        default:
            if (*character < 0x20)
            {
                escaped[0] = '\\';
                escaped[1] = 'u';
                escaped[2] = '0';
                escaped[3] = '0';
                escaped[4] = hex[*character >> 4];
                escaped[5] = hex[*character & 0x0f];
                escaped_length = sizeof(escaped);
            }
            else
            {
                escaped[0] = static_cast<char>(*character);
            }
            break;
        }

        if (length + escaped_length >= capacity)
        {
            return -EMSGSIZE;
        }
        for (size_t i = 0; i < escaped_length; ++i)
        {
            output[length++] = escaped[i];
        }
        output[length] = '\0';
    }

    return appendText(output, capacity, length, "\"");
}

int makeLoginPayload(const char *username, const char *password, char *payload, size_t capacity)
{
    size_t length = 0;
    payload[0] = '\0';

    int ret = appendText(payload, capacity, length, "{\"username\":");
    if (ret == 0)
    {
        ret = appendJsonString(payload, capacity, length, username);
    }
    if (ret == 0)
    {
        ret = appendText(payload, capacity, length, ",\"password\":");
    }
    if (ret == 0)
    {
        ret = appendJsonString(payload, capacity, length, password);
    }
    if (ret == 0)
    {
        ret =
            appendText(payload, capacity, length, ",\"_eventId\":\"submit\",\"submit\":\"login\"}");
    }
    return ret;
}
} // namespace

int InternetUGM::connect(const char *username, const char *password, k_timeout_t timeout)
{
    if (username == nullptr || username[0] == '\0' || password == nullptr || password[0] == '\0')
    {
        return -EINVAL;
    }

    if (!isConnected())
    {
        _logged_in = false;
    }
    if (_logged_in)
    {
        return 0;
    }

    int ret = WiFiService::connect(ugm_ssid, nullptr, timeout);
    if (ret != 0)
    {
        return ret;
    }
    printk("Joined %s, logging in as %s\n", ugm_ssid, username);

    char payload[512];
    ret = makeLoginPayload(username, password, payload, sizeof(payload));
    if (ret != 0)
    {
        return ret;
    }

    // Same request as the Arduino library: HTTPS without certificate verification
    // (setInsecure) and no Content-Type header (HTTPClient::POST does not add one).
    int status = postInternal(ugm_login_url, payload, true, nullptr, nullptr, nullptr);
    printk("UGM login HTTP status: %d\n", status);
    if (status >= 200 && status < 400)
    {
        _logged_in = true;
        return 0;
    }
    if (status >= 400 && status < 500)
    {
        return -EACCES;
    }
    if (status >= 500)
    {
        return -EIO;
    }
    return status;
}
