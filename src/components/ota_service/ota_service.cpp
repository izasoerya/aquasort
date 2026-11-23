#include "ota_service.h"

#include <cerrno>
#include <cstring>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

namespace
{
OTAService *active_service = nullptr;
uint16_t ota_http_port = 8080;
struct flash_img_context ota_flash_context {};
bool ota_upload_started = false;
bool ota_upload_in_progress = false;
http_client_ctx *ota_active_client = nullptr;

// Delay between staging the image and rebooting, so the 202 response reaches the client.
constexpr int ota_reboot_delay_ms = 2000;
k_work_delayable ota_reboot_work;

void otaRebootHandler(k_work *work)
{
    (void)work;
    printk("OTA: rebooting into new image\n");
    sys_reboot(SYS_REBOOT_COLD);
}

void setResponse(http_response_ctx *response, http_status status)
{
    response->status = status;
    response->headers = nullptr;
    response->header_count = 0;
    response->body = nullptr;
    response->body_len = 0;
    response->final_chunk = false;
}

extern "C" int otaHttpHandler(http_client_ctx *client,
                              http_transaction_status transaction_status,
                              const http_request_ctx *request,
                              http_response_ctx *response,
                              void *user_data)
{
    (void)client;
    OTAService *service = static_cast<OTAService *>(user_data);
    active_service = service;

    if (transaction_status == HTTP_SERVER_TRANSACTION_ABORTED)
    {
        ota_upload_started = false;
        ota_upload_in_progress = false;
        ota_active_client = nullptr;
        service->setUploadError(400);
        setResponse(response, HTTP_400_BAD_REQUEST);
        response->final_chunk = true;
        return 0;
    }

    if (transaction_status == HTTP_SERVER_TRANSACTION_COMPLETE)
    {
        if (ota_active_client == client)
        {
            ota_upload_in_progress = false;
            ota_active_client = nullptr;
        }
        return 0;
    }

    if (ota_upload_in_progress && ota_active_client != nullptr && ota_active_client != client)
    {
        setResponse(response, HTTP_409_CONFLICT);
        response->final_chunk = true;
        service->setUploadError(409);
        return 0;
    }

    if (ota_upload_in_progress && ota_upload_started &&
        request != nullptr && ota_flash_context.flash_area == nullptr)
    {
        setResponse(response, HTTP_500_INTERNAL_SERVER_ERROR);
        response->final_chunk = true;
        service->setUploadError(500);
        return 0;
    }

    if (!ota_upload_in_progress)
    {
        ota_upload_in_progress = true;
        ota_upload_started = false;
        ota_active_client = client;
        service->setUploadStarted();
    }

    if (!ota_upload_started)
    {
        ota_flash_context = {};
        uint8_t upload_slot = flash_img_get_upload_slot();
        int ret = boot_erase_img_bank(upload_slot);
        if (ret != 0)
        {
            printk("OTA: failed to erase slot %u: %d\n", upload_slot, ret);
            ota_upload_started = false;
            ota_upload_in_progress = false;
            service->setUploadError(500);
            setResponse(response, HTTP_500_INTERNAL_SERVER_ERROR);
            response->final_chunk = true;
            return 0;
        }

        ret = flash_img_init(&ota_flash_context);
        if (ret != 0)
        {
            printk("OTA: failed to initialize flash image context: %d\n", ret);
            ota_upload_in_progress = false;
            service->setUploadError(500);
            setResponse(response, HTTP_500_INTERNAL_SERVER_ERROR);
            response->final_chunk = true;
            return 0;
        }

        ota_upload_started = true;
    }

    if (request == nullptr)
    {
        setResponse(response, HTTP_400_BAD_REQUEST);
        response->final_chunk = true;
        service->setUploadError(400);
        return 0;
    }

    size_t received = service->bytesReceived();
    if (request->data_len > ota_flash_context.flash_area->fa_size - received)
    {
        ota_upload_in_progress = false;
        ota_upload_started = false;
        service->setUploadError(413);
        setResponse(response, HTTP_413_PAYLOAD_TOO_LARGE);
        response->final_chunk = true;
        return 0;
    }

    if (request->data_len > 0)
    {
        bool final_chunk = transaction_status == HTTP_SERVER_REQUEST_DATA_FINAL;
        int ret = flash_img_buffered_write(&ota_flash_context,
                                           request->data,
                                           request->data_len,
                                           final_chunk);
        if (ret != 0)
        {
            printk("OTA: flash write failed: %d\n", ret);
            ota_upload_in_progress = false;
            ota_upload_started = false;
            service->setUploadError(500);
            setResponse(response, HTTP_500_INTERNAL_SERVER_ERROR);
            response->final_chunk = true;
            return 0;
        }
        service->addReceivedBytes(request->data_len);
    }

    if (transaction_status == HTTP_SERVER_REQUEST_DATA_FINAL)
    {
        if (service->bytesReceived() == 0)
        {
            ota_upload_in_progress = false;
            ota_upload_started = false;
            service->setUploadError(400);
            setResponse(response, HTTP_400_BAD_REQUEST);
        }
        else
        {
            int ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
            if (ret != 0)
            {
                printk("OTA: failed to mark image pending: %d\n", ret);
                ota_upload_in_progress = false;
                ota_upload_started = false;
                service->setUploadError(500);
                setResponse(response, HTTP_500_INTERNAL_SERVER_ERROR);
            }
            else
            {
                ota_upload_in_progress = false;
                ota_upload_started = false;
                service->setUploadStaged();
                setResponse(response, HTTP_202_ACCEPTED);
                printk("OTA: image staged (%u bytes), rebooting in %d ms\n",
                       (unsigned)service->bytesReceived(), ota_reboot_delay_ms);
                k_work_schedule(&ota_reboot_work, K_MSEC(ota_reboot_delay_ms));
            }
        }
        response->final_chunk = true;
        return 0;
    }

    setResponse(response, HTTP_200_OK);
    return 0;
}

struct ota_resource_detail_dynamic
{
    http_resource_detail_dynamic detail;
};

ota_resource_detail_dynamic ota_resource_detail = {
    .detail = {
        .common = {
            .bitmask_of_supported_http_methods = BIT(HTTP_POST),
            .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        },
        .cb = otaHttpHandler,
        .user_data = nullptr,
    },
};
}

HTTP_SERVICE_DEFINE(ota_http_service,
                    nullptr,
                    &ota_http_port,
                    1,
                    2,
                    nullptr,
                    nullptr,
                    nullptr);

HTTP_RESOURCE_DEFINE(ota_resource, ota_http_service, "/ota",
                     &ota_resource_detail.detail);

OTAService::OTAService()
{
    active_service = this;
    ota_resource_detail.detail.user_data = this;
    k_work_init_delayable(&ota_reboot_work, otaRebootHandler);
}

int OTAService::confirmImage()
{
    if (boot_is_img_confirmed())
        return 0;

    int ret = boot_write_img_confirmed();
    printk("OTA: confirming running image: %d\n", ret);
    return ret;
}

int OTAService::start(uint16_t port)
{
    ota_http_port = port;
    return http_server_start();
}

int OTAService::stop()
{
    ota_upload_in_progress = false;
    ota_upload_started = false;
    _status = OTAStatus::Idle;
    return http_server_stop();
}

OTAStatus OTAService::status() const
{
    return _status;
}

size_t OTAService::bytesReceived() const
{
    return _bytes_received;
}

int OTAService::lastHttpStatus() const
{
    return _last_http_status;
}

void OTAService::setUploadStarted()
{
    _status = OTAStatus::Receiving;
    _bytes_received = 0;
    _last_http_status = 200;
}

void OTAService::addReceivedBytes(size_t bytes)
{
    _bytes_received += bytes;
}

void OTAService::setUploadStaged()
{
    _status = OTAStatus::Staged;
    _last_http_status = 202;
}

void OTAService::setUploadError(int http_status)
{
    _status = OTAStatus::Error;
    _last_http_status = http_status;
}
