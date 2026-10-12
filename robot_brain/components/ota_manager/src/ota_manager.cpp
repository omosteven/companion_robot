#include "ota_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <new>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_partition.h"

namespace
{

constexpr char TAG[] = "OTA";

constexpr char OTA_URI[] = "/ota";

constexpr size_t OTA_BUFFER_SIZE = 4096;

httpd_handle_t server = nullptr;

esp_err_t ota_post_handler(
    httpd_req_t *req)
{
    ESP_LOGI(
        TAG,
        "OTA upload started");

    /*
     * Get the partition where the new firmware
     * will be written.
     */
    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(nullptr);

    if (update_partition == nullptr)
    {
        ESP_LOGE(
            TAG,
            "No OTA update partition available");

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "No OTA update partition available");

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Writing firmware to partition: %s",
        update_partition->label);

    ESP_LOGI(
        TAG,
        "Partition address: 0x%lx",
        static_cast<unsigned long>(
            update_partition->address));

    ESP_LOGI(
        TAG,
        "Partition size: %lu bytes",
        static_cast<unsigned long>(
            update_partition->size));

    /*
     * Start the OTA operation.
     */
    esp_ota_handle_t ota_handle = 0;

    esp_err_t err =
        esp_ota_begin(
            update_partition,
            OTA_SIZE_UNKNOWN,
            &ota_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_begin() failed: %s",
            esp_err_to_name(err));

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "OTA begin failed");

        return err;
    }

    /*
     * Allocate a small streaming buffer.
     *
     * We intentionally do NOT allocate the entire
     * firmware image.
     */
    uint8_t *buffer =
        new (std::nothrow) uint8_t[OTA_BUFFER_SIZE];

    if (buffer == nullptr)
    {
        ESP_LOGE(
            TAG,
            "Failed to allocate OTA buffer");

        esp_ota_abort(ota_handle);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "OTA buffer allocation failed");

        return ESP_ERR_NO_MEM;
    }

    size_t total_received = 0;

    while (true)
    {
        int received =
            httpd_req_recv(
                req,
                reinterpret_cast<char *>(buffer),
                OTA_BUFFER_SIZE);

        if (received == HTTPD_SOCK_ERR_TIMEOUT)
        {
            ESP_LOGW(
                TAG,
                "HTTP receive timeout");

            continue;
        }

        if (received <= 0)
        {
            if (received == 0)
            {
                ESP_LOGE(
                    TAG,
                    "OTA connection closed unexpectedly");
            }
            else
            {
                ESP_LOGE(
                    TAG,
                    "HTTP receive error: %d",
                    received);
            }

            delete[] buffer;

            esp_ota_abort(ota_handle);

            return ESP_FAIL;
        }

        /*
         * Write this chunk directly into
         * the inactive OTA partition.
         */
        err =
            esp_ota_write(
                ota_handle,
                buffer,
                received);

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "esp_ota_write() failed: %s",
                esp_err_to_name(err));

            delete[] buffer;

            esp_ota_abort(ota_handle);

            httpd_resp_send_err(
                req,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                "OTA write failed");

            return err;
        }

        total_received +=
            static_cast<size_t>(received);

        /*
         * Log every ~256 KB instead of every packet.
         */
        if ((total_received % (256 * 1024)) <
            OTA_BUFFER_SIZE)
        {
            ESP_LOGI(
                TAG,
                "OTA progress: %lu bytes",
                static_cast<unsigned long>(
                    total_received));
        }

        /*
         * HTTP content length tells us exactly
         * when the upload is complete.
         */
        if (total_received >=
            static_cast<size_t>(req->content_len))
        {
            break;
        }
    }

    delete[] buffer;

    ESP_LOGI(
        TAG,
        "OTA upload complete: %lu bytes",
        static_cast<unsigned long>(
            total_received));

    /*
     * Finalize the OTA image.
     *
     * This verifies the image format and
     * completes the OTA write operation.
     */
    err =
        esp_ota_end(ota_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_end() failed: %s",
            esp_err_to_name(err));

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Invalid OTA image");

        return err;
    }

    /*
     * Tell the bootloader to boot this
     * newly uploaded partition.
     */
    err =
        esp_ota_set_boot_partition(
            update_partition);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_set_boot_partition() failed: %s",
            esp_err_to_name(err));

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Failed to select OTA partition");

        return err;
    }

    ESP_LOGI(
        TAG,
        "OTA partition selected successfully");

    /*
     * Tell the client that the upload succeeded
     * before rebooting.
     */
    const char *response =
        "OTA successful. Rebooting...\n";

    httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN);

    /*
     * Give the TCP stack a little time to send
     * the HTTP response before rebooting.
     */
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGW(
        TAG,
        "Restarting into new firmware...");

    esp_restart();

    return ESP_OK;
}

} // namespace

esp_err_t ota_manager_start()
{
    if (server != nullptr)
    {
        ESP_LOGW(
            TAG,
            "OTA server already running");

        return ESP_OK;
    }

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    /*
     * Keep OTA isolated in its own task.
     */
    config.server_port = 80;

    config.max_uri_handlers = 4;

    config.stack_size = 8192;

    config.recv_wait_timeout = 10;

    config.send_wait_timeout = 10;

    httpd_uri_t ota_uri{};

    ota_uri.uri = OTA_URI;

    ota_uri.method = HTTP_POST;

    ota_uri.handler = ota_post_handler;

    ota_uri.user_ctx = nullptr;

    esp_err_t err =
        httpd_start(
            &server,
            &config);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to start HTTP server: %s",
            esp_err_to_name(err));

        server = nullptr;

        return err;
    }

    err =
        httpd_register_uri_handler(
            server,
            &ota_uri);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to register OTA URI: %s",
            esp_err_to_name(err));

        httpd_stop(server);

        server = nullptr;

        return err;
    }

    ESP_LOGI(
        TAG,
        "OTA HTTP server started");

    ESP_LOGI(
        TAG,
        "OTA endpoint: POST http://192.168.4.1%s",
        OTA_URI);

    return ESP_OK;
}

esp_err_t ota_manager_stop()
{
    if (server == nullptr)
    {
        return ESP_OK;
    }

    esp_err_t err =
        httpd_stop(server);

    server = nullptr;

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to stop HTTP server: %s",
            esp_err_to_name(err));

        return err;
    }

    ESP_LOGI(
        TAG,
        "OTA HTTP server stopped");

    return ESP_OK;
}