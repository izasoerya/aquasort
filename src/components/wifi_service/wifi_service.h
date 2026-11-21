#pragma once

#include <zephyr/kernel.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/tls_credentials.h>

class WiFiService
{
protected:
    int postInternal(const char *url, const char *payload, bool secure,
                     const char *ca_cert,
                     const char *const *header_fields);

    int postSecureWithHeaders(const char *url, const char *payload,
                              const char *ca_cert,
                              const char *const *header_fields);

private:
    net_if *_iface = nullptr;
    net_mgmt_event_callback _wifi_cb;
    net_mgmt_event_callback _ipv4_cb;
    k_sem _connected_sem;
    k_sem _ipv4_sem;
    int _connect_status = -1;

    static void onWifiEvent(net_mgmt_event_callback *cb, uint64_t mgmt_event, net_if *iface)
    {
        WiFiService *self = CONTAINER_OF(cb, WiFiService, _wifi_cb);

        if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT)
        {
            const wifi_status *status = static_cast<const wifi_status *>(cb->info);
            self->_connect_status = status->status;
            if (status->status == 0)
            {
                k_sem_give(&self->_connected_sem);
            }
            else
            {
                k_sem_give(&self->_connected_sem);
            }
        }
        else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT)
        {
            self->_connect_status = -1;
        }
    }

    static void onIpv4Event(net_mgmt_event_callback *cb, uint64_t mgmt_event, net_if *iface)
    {
        auto *self = CONTAINER_OF(cb, WiFiService, _ipv4_cb);

        if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD)
        {
            for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; ++i)
            {
                const net_if_addr_ipv4 &address = iface->config.ip.ipv4->unicast[i];
                if (address.ipv4.addr_type == NET_ADDR_DHCP)
                {
                    k_sem_give(&self->_ipv4_sem);
                }
            }
        }
    }

public:
    WiFiService();
    virtual ~WiFiService() = default;

    /**
     * @brief           Retrieve Wi-Fi connection state
     * @retval `bool`   Connectivity state
     */
    bool isConnected();

    /**
     * @brief           Connect to Wi-Fi using an SSID and pre-shared key.
     * @param ssid      Null-terminated Wi-Fi network name.
     * @param psk       Null-terminated pre-shared key.
     * @param timeout   Maximum time to wait for association and DHCP for each stage.
     *
     * @retval `0`            Wi-Fi association and DHCP succeeded.
     * @retval `-ENODEV`      Default network interface unavailable.
     * @retval `-ETIMEDOUT`   Association or DHCP timed out.
     * @retval `-EIO`         Wi-Fi connection failed.
     */
    int connect(const char *ssid, const char *psk, k_timeout_t timeout = K_SECONDS(15));

    /**
     * @brief Send an HTTP POST request.
     * @param url     Plain HTTP URL, for example "http://example.com/api".
     * @param payload Null-terminated request payload.
     * @retval HTTP status code on success.
     * @retval Negative errno value on connection or request failure.
     */
    virtual int post(const char *url, const char *payload);

    /**
     * @brief Send an HTTPS POST request.
     * @param url     HTTPS URL, for example "https://example.com/api".
     * @param payload Null-terminated request payload.
     * @param ca_cert PEM CA certificate, or nullptr to disable verification.
     * @retval HTTP status code on success.
     * @retval Negative errno value on connection or request failure.
     */
    int postSecure(const char *url, const char *payload, const char *ca_cert);
};