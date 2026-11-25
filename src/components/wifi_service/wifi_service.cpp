#include "wifi_service.h"
#include <cerrno>
#include <cstdio>
#include <cstring>

WiFiService::WiFiService()
{
    k_sem_init(&_connected_sem, 0, 1);
    k_sem_init(&_ipv4_sem, 0, 1);
}

bool WiFiService::isConnected()
{
    return _connect_status == 0 ? true : false;
}

int WiFiService::setHostname(const char *hostname)
{
    if (hostname == nullptr)
    {
        return -EINVAL;
    }

    size_t length = strlen(hostname);
    if (length == 0)
    {
        return -EINVAL;
    }

    return net_hostname_set(hostname, length);
}

int WiFiService::connect(const char *ssid, const char *psk, k_timeout_t timeout)
{
    if (ssid == nullptr || ssid[0] == '\0')
        return -EINVAL;

    _iface = net_if_get_default();
    if (!_iface)
        return -ENODEV;

    if (isConnected())
        return 0;

    // Register callbacks only once: adding the same node twice corrupts net_mgmt's list.
    if (!_callbacks_registered)
    {
        net_mgmt_init_event_callback(&_wifi_cb, &WiFiService::onWifiEvent,
                                     NET_EVENT_WIFI_CONNECT_RESULT |
                                         NET_EVENT_WIFI_DISCONNECT_RESULT);
        net_mgmt_add_event_callback(&_wifi_cb);

        net_mgmt_init_event_callback(&_ipv4_cb, &WiFiService::onIpv4Event, NET_EVENT_IPV4_ADDR_ADD);
        net_mgmt_add_event_callback(&_ipv4_cb);
        _callbacks_registered = true;
    }

    k_sem_reset(&_connected_sem);
    k_sem_reset(&_ipv4_sem);

    wifi_connect_req_params params = {};
    params.ssid = reinterpret_cast<const uint8_t *>(ssid);
    params.ssid_length = strlen(ssid);
    if (psk != nullptr && psk[0] != '\0')
    {
        params.psk = reinterpret_cast<const uint8_t *>(psk);
        params.psk_length = strlen(psk);
        params.security = WIFI_SECURITY_TYPE_PSK;
    }
    else
    {
        params.security = WIFI_SECURITY_TYPE_NONE;
    }
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, _iface, &params, sizeof(params));
    if (ret == -EALREADY)
    {
        // The driver is already connecting on its own (CONFIG_ESP32_WIFI_STA_RECONNECT).
        // It rejects our request and also raises a synthetic CONN_FAIL result for it, so
        // the result event cannot be trusted here: wait until its own attempt succeeds.
        printk("[wifi] driver already connecting, waiting for it\n");
        k_timepoint_t deadline = sys_timepoint_calc(timeout);
        while (!isConnected())
        {
            if (sys_timepoint_expired(deadline))
            {
                printk("[wifi] timed out waiting for driver to join %s\n", ssid);
                return -ETIMEDOUT;
            }
            k_msleep(100);
        }
    }
    else if (ret != 0)
    {
        return ret;
    }
    else
    {
        // 1. Wait for Wi-Fi association
        printk("[wifi] joining %s\n", ssid);
        if (k_sem_take(&_connected_sem, timeout) != 0)
        {
            printk("[wifi] timed out joining %s (no connect result)\n", ssid);
            return -ETIMEDOUT;
        }
        if (_connect_status != 0)
        {
            // 1 = fail, 2 = wrong password, 3 = timeout, 4 = AP not found
            printk("Wi-Fi connect result status: %d\n", _connect_status);
            return -EIO;
        }
    }

    // 2. Wait for DHCP IPv4 lease, unless one is already assigned
    if (localIP() == nullptr && k_sem_take(&_ipv4_sem, timeout) != 0)
    {
        printk("[wifi] joined %s but timed out waiting for DHCP\n", ssid);
        return -ETIMEDOUT;
    }

    return 0;
}

const char *WiFiService::localIP()
{
    static char address[NET_IPV4_ADDR_LEN];

    net_if *iface = _iface != nullptr ? _iface : net_if_get_default();
    if (iface == nullptr)
        return nullptr;

    struct net_in_addr *ipv4 = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
    if (ipv4 == nullptr)
        return nullptr;

    return net_addr_ntop(NET_AF_INET, ipv4, address, sizeof(address));
}

esp_err_t WiFiService::setTxPower(wifi_tx_power power)
{
    return esp_wifi_set_max_tx_power(static_cast<int8_t>(power));
}

int WiFiService::post(const char *url, const char *payload)
{
    return postInternal(url, payload, false, nullptr, nullptr);
}

int WiFiService::postSecure(const char *url, const char *payload, const char *ca_cert)
{
    return postSecureWithHeaders(url, payload, ca_cert, nullptr);
}

int WiFiService::postSecureWithHeaders(const char *url, const char *payload, const char *ca_cert,
                                       const char *const *header_fields)
{
    return postInternal(url, payload, true, ca_cert, header_fields);
}

int WiFiService::postInternal(const char *url, const char *payload, bool secure,
                              const char *ca_cert, const char *const *header_fields,
                              const char *content_type)
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

    net_if *iface = _iface != nullptr ? _iface : net_if_get_default();
    if (iface != nullptr)
    {
        struct net_in_addr gateway = net_if_ipv4_get_gw(iface);
        struct net_in_addr *local = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        char gateway_text[NET_IPV4_ADDR_LEN];
        char local_text[NET_IPV4_ADDR_LEN];
        printk("IPv4 local=%s gateway=%s\n",
               local != nullptr ? net_addr_ntop(NET_AF_INET, local, local_text, sizeof(local_text))
                                : "none",
               net_addr_ntop(NET_AF_INET, &gateway, gateway_text, sizeof(gateway_text)));
    }

    zsock_addrinfo *addresses = nullptr;
    int ret = zsock_getaddrinfo(host, port, &hints, &addresses);
    printk("DNS host=%s port=%s ret=%d addresses=%p\n", host, port, ret, addresses);
    if (ret != 0 || addresses == nullptr)
    {
        return -EHOSTUNREACH;
    }

    int sock = -1;
    zsock_addrinfo *address = addresses;
    for (; address != nullptr; address = address->ai_next)
    {
        printk("DNS address: family=%d socktype=%d protocol=%d secure=%d\n", address->ai_family,
               address->ai_socktype, address->ai_protocol, secure);
        char resolved_address[NET_IPV4_ADDR_LEN];
        printk("Resolved address: %s\n",
               net_addr_ntop(NET_AF_INET, &net_sin(address->ai_addr)->sin_addr, resolved_address,
                             sizeof(resolved_address)));

        int protocol = secure ? IPPROTO_TLS_1_2 : IPPROTO_TCP;
        sock = zsock_socket(AF_INET, SOCK_STREAM, protocol);
        printk("Socket create ret=%d family=%d type=%d protocol=%d errno=%d\n", sock, AF_INET,
               SOCK_STREAM, protocol, errno);
        if (sock < 0)
        {
            printk("Socket create failed for address=%p errno=%d\n", address, errno);
            continue;
        }

        if (secure)
        {
            ret = 0;
            int peer_verify = ZSOCK_TLS_PEER_VERIFY_NONE;

            if (ret == 0)
            {
                ret = zsock_setsockopt(sock, ZSOCK_SOL_TLS, ZSOCK_TLS_PEER_VERIFY, &peer_verify,
                                       sizeof(peer_verify));
            }

            if (ret == 0)
            {
                ret = zsock_setsockopt(sock, ZSOCK_SOL_TLS, ZSOCK_TLS_HOSTNAME, host,
                                       strlen(host) + 1);
            }

            if (ret < 0)
            {
                printk("TLS socket setup failed ret=%d errno=%d\n", ret, errno);
                zsock_close(sock);
                sock = -1;
                continue;
            }
        }

        ret = zsock_connect(sock, address->ai_addr, address->ai_addrlen);
        printk("TCP connect ret=%d errno=%d secure=%d\n", ret, errno, secure);
        if (ret == 0)
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

    // Zephyr's http_client_req() is not used here: its parser rejects the valid
    // "Content-Range: */*" header Supabase sends on inserts (HPE_INVALID_CONTENT_RANGE).
    // We only need the status code, so send the request and read the status line directly.
    int http_status = sendRequest(sock, host, request_path, payload, header_fields, content_type);
    zsock_close(sock);
    return http_status;
}

static int sendAll(int sock, const char *data, size_t length)
{
    while (length > 0)
    {
        ssize_t sent = zsock_send(sock, data, length, 0);
        if (sent < 0)
        {
            return -errno;
        }
        data += sent;
        length -= static_cast<size_t>(sent);
    }
    return 0;
}

int WiFiService::sendRequest(int sock, const char *host, const char *path, const char *payload,
                             const char *const *header_fields, const char *content_type)
{
    timeval timeout = {};
    timeout.tv_sec = 10;
    zsock_setsockopt(sock, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO, &timeout, sizeof(timeout));

    size_t payload_length = strlen(payload);
    char header[1024];
    int length = snprintf(header, sizeof(header),
                          "POST %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "Content-Length: %u\r\n"
                          "Connection: close\r\n",
                          path, host, static_cast<unsigned>(payload_length));
    if (content_type != nullptr && length >= 0 && static_cast<size_t>(length) < sizeof(header))
    {
        length += snprintf(header + length, sizeof(header) - length, "Content-Type: %s\r\n",
                           content_type);
    }
    for (size_t i = 0; header_fields != nullptr && header_fields[i] != nullptr; ++i)
    {
        if (length < 0 || static_cast<size_t>(length) >= sizeof(header))
        {
            break;
        }
        // Each header field already ends with "\r\n".
        length += snprintf(header + length, sizeof(header) - length, "%s", header_fields[i]);
    }
    if (length >= 0 && static_cast<size_t>(length) < sizeof(header))
    {
        length += snprintf(header + length, sizeof(header) - length, "\r\n");
    }
    if (length < 0 || static_cast<size_t>(length) >= sizeof(header))
    {
        return -EMSGSIZE;
    }

    int ret = sendAll(sock, header, static_cast<size_t>(length));
    if (ret == 0)
    {
        ret = sendAll(sock, payload, payload_length);
    }
    if (ret < 0)
    {
        printk("HTTP send failed: %d\n", ret);
        return ret;
    }

    // Read until the end of the headers (or the buffer fills); the status line comes first.
    char response[768];
    size_t received = 0;
    while (received < sizeof(response) - 1)
    {
        ssize_t n = zsock_recv(sock, response + received, sizeof(response) - 1 - received, 0);
        if (n < 0)
        {
            printk("HTTP recv failed: errno=%d\n", errno);
            return errno == EAGAIN ? -ETIMEDOUT : -errno;
        }
        if (n == 0)
        {
            break; // server closed the connection
        }
        received += static_cast<size_t>(n);
        response[received] = '\0';
        if (strstr(response, "\r\n\r\n") != nullptr)
        {
            break;
        }
    }
    response[received] = '\0';

    // Status line: "HTTP/1.1 201 Created"
    unsigned major = 0, minor = 0;
    int status = 0;
    if (sscanf(response, "HTTP/%u.%u %d", &major, &minor, &status) != 3)
    {
        printk("HTTP bad response (%u bytes)\n", static_cast<unsigned>(received));
        return -EBADMSG;
    }

    if (status < 200 || status >= 300)
    {
        // Print whatever part of the error body arrived, e.g. PostgREST's JSON message.
        const char *body = strstr(response, "\r\n\r\n");
        printk("HTTP %d error body: %s\n", status, body != nullptr ? body + 4 : "");
    }

    return status;
}
