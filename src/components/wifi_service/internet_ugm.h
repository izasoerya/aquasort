#pragma once

#include "wifi_service.h"

class InternetUGM : public WiFiService
{
public:
    /**
     * @brief Connect to UGM-Hotspot and authenticate with UGM credentials.
     * @param username UGM account username.
     * @param password UGM account password.
     * @param timeout  Maximum time to wait for Wi-Fi association and DHCP.
     *
     * @retval 0         Wi-Fi connection and UGM authentication request succeeded.
     * @retval -EINVAL   A credential is null or empty.
     * @retval -EACCES   UGM returned an HTTP client error.
     * @retval Negative  Other Wi-Fi or HTTP error.
     */
    int connect(const char *username, const char *password,
                k_timeout_t timeout = K_SECONDS(15)) override;

private:
    // Set after a successful SSO login; cleared when Wi-Fi drops so we log in again.
    bool _logged_in = false;
};
