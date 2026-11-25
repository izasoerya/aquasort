#pragma once

#include "esp_wifi.h"

#include <zephyr/kernel.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

enum wifi_tx_power
{
    WIFI_TX_POWER_2_0_DBM = 8,
    WIFI_TX_POWER_5_0_DBM = 20,
    WIFI_TX_POWER_7_0_DBM = 28,
    WIFI_TX_POWER_8_5_DBM = 34,
    WIFI_TX_POWER_11_0_DBM = 44,
    WIFI_TX_POWER_13_0_DBM = 52,
    WIFI_TX_POWER_14_0_DBM = 56,
    WIFI_TX_POWER_15_0_DBM = 60,
    WIFI_TX_POWER_16_5_DBM = 66,
    WIFI_TX_POWER_18_0_DBM = 72,
    WIFI_TX_POWER_20_0_DBM = 80,
};

class WiFiService
{
protected:
    // content_type == nullptr omits the Content-Type header.
    int postInternal(const char *url, const char *payload, bool secure, const char *ca_cert,
                     const char *const *header_fields,
                     const char *content_type = "application/json");

    int postSecureWithHeaders(const char *url, const char *payload, const char *ca_cert,
                              const char *const *header_fields);

    int sendRequest(int sock, const char *host, const char *path, const char *payload,
                    const char *const *header_fields, const char *content_type);

private:
    net_if *_iface = nullptr;
    net_mgmt_event_callback _wifi_cb;
    net_mgmt_event_callback _ipv4_cb;
    k_sem _connected_sem;
    k_sem _ipv4_sem;
    int _connect_status = -1;
    bool _callbacks_registered = false;

    static void onWifiEvent(net_mgmt_event_callback *cb, uint64_t mgmt_event, net_if *iface)
    {
        WiFiService *self = CONTAINER_OF(cb, WiFiService, _wifi_cb);

        if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT)
        {
            const wifi_status *status = static_cast<const wifi_status *>(cb->info);
            self->_connect_status = status->status;
            // 0 = ok, 1 = fail, 2 = wrong password, 3 = timeout, 4 = AP not found
            printk("[wifi] connect result: %d\n", status->status);
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
            printk("[wifi] disconnected\n");
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
                    printk("[wifi] DHCP address assigned\n");
                    k_sem_give(&self->_ipv4_sem);
                }
            }
        }
    }

public:
    WiFiService();
    virtual ~WiFiService() = default;

    /**
     * @brief Set the network hostname for this device.
     * @param hostname Null-terminated device hostname.
     * @retval 0         Hostname set successfully.
     * @retval -ENOTSUP  Dynamic network hostnames are disabled.
     * @retval -EINVAL   The hostname is null or invalid.
     * @retval Negative  Other Zephyr network hostname error.
     */
    int setHostname(const char *hostname);

    /**
     * @brief           Retrieve Wi-Fi connection state
     * @retval `bool`   Connectivity state
     */
    bool isConnected();

    /**
     * @brief           Connect to Wi-Fi using an SSID and pre-shared key.
     * @param ssid      Null-terminated Wi-Fi network name.
     * @param psk       Null-terminated pre-shared key, or null/empty for an open network.
     * @param timeout   Maximum time to wait for association and DHCP for each stage.
     *
     * @retval `0`            Wi-Fi association and DHCP succeeded.
     * @retval `-ENODEV`      Default network interface unavailable.
     * @retval `-ETIMEDOUT`   Association or DHCP timed out.
     * @retval `-EIO`         Wi-Fi connection failed.
     */
    virtual int connect(const char *ssid, const char *psk, k_timeout_t timeout = K_SECONDS(15));

    /**
     * @brief Drop the current association and stop the driver's auto-reconnect to it.
     * @retval 0         Disconnected, or nothing to disconnect.
     * @retval -ENODEV   Default network interface unavailable.
     */
    int disconnect();

    const char *localIP();

    esp_err_t setTxPower(wifi_tx_power power = WIFI_TX_POWER_8_5_DBM);

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