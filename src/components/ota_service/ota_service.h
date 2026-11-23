#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief State of an OTA firmware upload.
 */
enum class OTAStatus
{
    Idle,
    Receiving,
    Staged,
    Error
};

/**
 * @brief Receives firmware binary uploads and stages them in the MCUboot secondary slot.
 *
 * The service exposes POST /ota on the configured HTTP port. The request body must be
 * a raw, signed MCUboot-compatible firmware binary. A successful upload stages the
 * image, marks it as a test upgrade and reboots the device ~2 s later. The new image
 * must call confirmImage() once it is healthy, otherwise MCUboot reverts it on the
 * next reset.
 */
class OTAService
{
public:
    /**
     * @brief Construct an OTA service and bind it to the `/ota` HTTP resource.
     */
    OTAService();

    /**
     * @brief Start the HTTP OTA server.
     *
     * @param port TCP port to listen on.
     *
     * @retval 0         The server started successfully.
     * @retval -EALREADY The HTTP server is already running.
     * @retval Negative  Other Zephyr HTTP server error.
     */
    int start(uint16_t port = 8080);

    /**
     * @brief Stop the HTTP OTA server.
     *
     * @retval 0         The server stopped successfully.
     * @retval Negative  Zephyr HTTP server error.
     */
    int stop();

    /**
     * @brief Mark the running image as good so MCUboot keeps it.
     *
     * After an OTA, MCUboot boots the new image in test mode. If this is not
     * called before the next reset, MCUboot rolls back to the previous image.
     * No-op if the image is already confirmed.
     *
     * @retval 0         Image is confirmed.
     * @retval Negative  Zephyr/flash errno.
     */
    static int confirmImage();

    /**
     * @brief Get the current OTA upload state.
     *
     * @retval OTAStatus Current state of the OTA upload.
     */
    OTAStatus status() const;

    /**
     * @brief Get the number of firmware bytes received and written during the current upload.
     *
     * @retval size_t Number of bytes received by the upload handler.
     */
    size_t bytesReceived() const;

    /**
     * @brief Get the result of the most recent OTA upload.
     *
     * @retval 0         The last upload succeeded.
     * @retval Positive  HTTP status code describing an upload failure.
     * @retval Negative  Zephyr/flash errno for a local flash failure.
     */
    int lastHttpStatus() const;

    /**
     * @brief Mark the beginning of a firmware upload.
     *
     * @retval void No return value.
     */
    void setUploadStarted();

    /**
     * @brief Add bytes written during the active firmware upload.
     *
     * @param bytes Number of bytes written in the latest HTTP fragment.
     *
     * @retval void No return value.
     */
    void addReceivedBytes(size_t bytes);

    /**
     * @brief Mark the uploaded image as staged (the reboot is scheduled by the HTTP handler).
     *
     * @retval void No return value.
     */
    void setUploadStaged();

    /**
     * @brief Mark the upload as failed.
     *
     * @param http_status HTTP status code describing the failure.
     *
     * @retval void No return value.
     */
    void setUploadError(int http_status);

private:
    OTAStatus _status = OTAStatus::Idle;
    size_t _bytes_received = 0;
    int _last_http_status = 200;
};
