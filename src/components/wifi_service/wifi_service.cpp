#include "wifi_service.h"

#include <cerrno>
#include <cstring>

#include <zephyr/net/socket.h>

static constexpr sec_tag_t HTTP_CA_CERT_TAG = 0x1001;

WiFiService::WiFiService()
{
    k_sem_init(&_connected_sem, 0, 1);
    k_sem_init(&_ipv4_sem, 0, 1);
}

bool WiFiService::isConnected()
{
    return _connect_status == 0 ? true : false;
}

int WiFiService::connect(const char *ssid, const char *psk, k_timeout_t timeout)
{
    _iface = net_if_get_default();
    if (!_iface)
    {
        return -ENODEV;
    }

    net_mgmt_init_event_callback(&_wifi_cb, &WiFiService::onWifiEvent,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&_wifi_cb);

    net_mgmt_init_event_callback(&_ipv4_cb, &WiFiService::onIpv4Event, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&_ipv4_cb);

    wifi_connect_req_params params = {};
    params.ssid = reinterpret_cast<const uint8_t *>(ssid);
    params.ssid_length = strlen(ssid);
    params.psk = reinterpret_cast<const uint8_t *>(psk);
    params.psk_length = strlen(psk);
    params.security = WIFI_SECURITY_TYPE_PSK;
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, _iface, &params, sizeof(params));
    if (ret != 0)
    {
        return ret;
    }

    // 1. Wait for Wi-Fi association
    if (k_sem_take(&_connected_sem, timeout) != 0)
    {
        return -ETIMEDOUT;
    }
    if (_connect_status != 0)
    {
        return -EIO;
    }

    // 2. Wait for DHCP IPv4 lease
    if (k_sem_take(&_ipv4_sem, timeout) != 0)
    {
        return -ETIMEDOUT;
    }

    return 0;
}

int WiFiService::post(const char *url, const char *payload)
{
    return postInternal(url, payload, false, nullptr, nullptr);
}

int WiFiService::postSecure(const char *url, const char *payload, const char *ca_cert)
{
    return postSecureWithHeaders(url, payload, ca_cert, nullptr);
}

int WiFiService::postSecureWithHeaders(const char *url, const char *payload,
                                       const char *ca_cert,
                                       const char *const *header_fields)
{
    return postInternal(url, payload, true, ca_cert, header_fields);
}

int WiFiService::postInternal(const char *url, const char *payload, bool secure,
                              const char *ca_cert,
                              const char *const *header_fields)
{
    if (url == nullptr || payload == nullptr)
    {
        return -EINVAL;
    }

    const char *scheme;
    char default_port[8];
    if (secure)
    {
        scheme = "https://";
        strcpy(default_port, "443");
    }
    else
    {
        scheme = "http://";
        strcpy(default_port, "80");
    }

    size_t scheme_length = strlen(scheme);
    if (strncmp(url, scheme, scheme_length) != 0)
    {
        return -ENOTSUP;
    }

    const char *authority = url + scheme_length;
    const char *path = strchr(authority, '/');
    const char *authority_end = path != nullptr ? path : authority + strlen(authority);

    char host[128];
    char port[8];
    strcpy(port, default_port);
    char request_path[256];

    size_t authority_length = static_cast<size_t>(authority_end - authority);
    if (authority_length == 0 || authority_length >= sizeof(host))
    {
        return -EINVAL;
    }

    memcpy(host, authority, authority_length);
    host[authority_length] = '\0';

    char *port_separator = strrchr(host, ':');
    if (port_separator != nullptr)
    {
        if (port_separator == host || strlen(port_separator + 1) >= sizeof(port))
        {
            return -EINVAL;
        }

        strcpy(port, port_separator + 1);
        *port_separator = '\0';
    }

    if (path == nullptr)
    {
        request_path[0] = '/';
        request_path[1] = '\0';
    }
    else
    {
        size_t path_length = strlen(path);
        if (path_length >= sizeof(request_path))
        {
            return -EMSGSIZE;
        }
        memcpy(request_path, path, path_length + 1);
    }

    zsock_addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    zsock_addrinfo *addresses = nullptr;
    int ret = zsock_getaddrinfo(host, port, &hints, &addresses);
    if (ret != 0 || addresses == nullptr)
    {
        return -EHOSTUNREACH;
    }

    int sock = -1;
    zsock_addrinfo *address = addresses;
    for (; address != nullptr; address = address->ai_next)
    {
        int protocol = address->ai_protocol;
        if (secure)
        {
            protocol = IPPROTO_TLS_1_2;
        }

        sock = zsock_socket(address->ai_family, address->ai_socktype, protocol);
        if (sock < 0)
        {
            continue;
        }

        if (secure)
        {
            bool verified = ca_cert != nullptr && ca_cert[0] != '\0';
            sec_tag_t sec_tag_list[] = {HTTP_CA_CERT_TAG};
            int peer_verify = verified ? ZSOCK_TLS_PEER_VERIFY_REQUIRED
                                       : ZSOCK_TLS_PEER_VERIFY_NONE;

            if (verified)
            {
                ret = tls_credential_delete(HTTP_CA_CERT_TAG,
                                            TLS_CREDENTIAL_CA_CERTIFICATE);
                ret = tls_credential_add(HTTP_CA_CERT_TAG,
                                         TLS_CREDENTIAL_CA_CERTIFICATE,
                                         ca_cert,
                                         strlen(ca_cert) + 1);
                if (ret == 0)
                {
                    ret = zsock_setsockopt(sock, ZSOCK_SOL_TLS,
                                           ZSOCK_TLS_SEC_TAG_LIST,
                                           sec_tag_list, sizeof(sec_tag_list));
                }
            }

            if (ret == 0)
            {
                ret = zsock_setsockopt(sock, ZSOCK_SOL_TLS,
                                       ZSOCK_TLS_PEER_VERIFY,
                                       &peer_verify, sizeof(peer_verify));
            }

            if (ret == 0 && verified)
            {
                ret = zsock_setsockopt(sock, ZSOCK_SOL_TLS,
                                       ZSOCK_TLS_HOSTNAME,
                                       host, strlen(host) + 1);
            }
            else if (ret == 0)
            {
                ret = zsock_setsockopt(sock, ZSOCK_SOL_TLS,
                                       ZSOCK_TLS_HOSTNAME,
                                       nullptr, 0);
            }

            if (ret < 0)
            {
                zsock_close(sock);
                sock = -1;
                continue;
            }
        }

        if (zsock_connect(sock, address->ai_addr, address->ai_addrlen) == 0)
        {
            break;
        }

        zsock_close(sock);
        sock = -1;
    }

    zsock_freeaddrinfo(addresses);

    if (sock < 0)
    {
        return -ECONNREFUSED;
    }

    uint8_t response_buffer[512];
    int http_status = 0;

    http_request request = {};
    request.method = HTTP_POST;
    request.url = request_path;
    request.host = host;
    request.port = port;
    request.protocol = "HTTP/1.1";
    request.content_type_value = "application/json";
    request.header_fields = header_fields;
    request.payload = payload;
    request.payload_len = strlen(payload);
    request.recv_buf = response_buffer;
    request.recv_buf_len = sizeof(response_buffer);
    request.response =
        [](http_response *response, http_final_call, void *user_data)
    {
        if (response != nullptr)
        {
            *static_cast<int *>(user_data) = response->http_status_code;
        }
        return 0;
    };

    ret = http_client_req(sock, &request, 10000, &http_status);
    zsock_close(sock);

    if (ret < 0)
    {
        if (secure)
        {
            tls_credential_delete(HTTP_CA_CERT_TAG,
                                  TLS_CREDENTIAL_CA_CERTIFICATE);
        }
        return ret;
    }

    if (secure)
    {
        tls_credential_delete(HTTP_CA_CERT_TAG,
                              TLS_CREDENTIAL_CA_CERTIFICATE);
    }
    return http_status;
}