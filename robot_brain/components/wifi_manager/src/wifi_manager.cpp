#include "wifi_manager.hpp"

#include <cstring>

#include "mdns.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace
{
constexpr char TAG[] = "WIFI";

// Temporary development credentials.
// We will move these to a proper configuration mechanism later.
constexpr char WIFI_SSID[] = "iPhone di Stefano"; //PRO’s MacBook Pro
constexpr char WIFI_PASSWORD[] = "stevenOmole"; //fjr45qeandcz8

// ============================================================
// STA configuration
// ============================================================

constexpr char WIFI_STA_SSID[] = "iPhone di Stefano";
constexpr char WIFI_STA_PASSWORD[] = "stevenOmole";

constexpr int MAX_STA_RETRY = 20;

int sta_retry_count = 0;
bool sta_connected = false;

// ============================================================
// SoftAP configuration
// ============================================================

constexpr char WIFI_AP_SSID[] = "BBU_Robot";
constexpr char WIFI_AP_PASSWORD[] = "robot1234";

constexpr uint8_t WIFI_AP_CHANNEL = 6;
constexpr uint8_t WIFI_AP_MAX_CONNECTIONS = 4;

// ============================================================
// Event handler instances
// ============================================================

esp_event_handler_instance_t wifi_event_instance;
esp_event_handler_instance_t ip_event_instance;

// ============================================================
// Network interfaces
// ============================================================

esp_netif_t *sta_netif = nullptr;
esp_netif_t *ap_netif = nullptr;

} // namespace


// ============================================================
// Wi-Fi event handler
// ============================================================

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    // --------------------------------------------------------
    // STA started
    // --------------------------------------------------------

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(
            TAG,
            "STA started");

        esp_err_t err = esp_wifi_connect();

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "esp_wifi_connect() failed: %s",
                esp_err_to_name(err));
        }

        return;
    }

    // --------------------------------------------------------
    // STA disconnected
    // --------------------------------------------------------

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        sta_connected = false;

        // if (sta_retry_count < MAX_STA_RETRY)
        // {
        //     sta_retry_count++;

        //     ESP_LOGW(
        //         TAG,
        //         "STA disconnected. Retrying (%d/%d)...",
        //         sta_retry_count,
        //         MAX_STA_RETRY);

        //     esp_err_t err = esp_wifi_connect();

        //     if (err != ESP_OK)
        //     {
        //         ESP_LOGE(
        //             TAG,
        //             "Reconnect failed: %s",
        //             esp_err_to_name(err));
        //     }
        // }
        // else
        // {
        //     ESP_LOGE(
        //         TAG,
        //         "STA failed after %d retries",
        //         MAX_STA_RETRY);
        // }

          ESP_LOGW(TAG, "STA disconnected");

        return;
    }

    // --------------------------------------------------------
    // SoftAP client connected
    // --------------------------------------------------------

   if (event_base == WIFI_EVENT &&
    event_id == WIFI_EVENT_AP_STACONNECTED)
{
    const auto *event =
        static_cast<wifi_event_ap_staconnected_t *>(event_data);

    ESP_LOGI(
        TAG,
        "AP client connected, AID=%d",
        event->aid);

    return;
}

    // --------------------------------------------------------
    // SoftAP client disconnected
    // --------------------------------------------------------

if (event_base == WIFI_EVENT &&
    event_id == WIFI_EVENT_AP_STADISCONNECTED)
{
    const auto *event =
        static_cast<wifi_event_ap_stadisconnected_t *>(event_data);

    ESP_LOGI(
        TAG,
        "AP client disconnected, AID=%d",
        event->aid);

    return;
}

    // --------------------------------------------------------
    // STA received IP address
    // --------------------------------------------------------

    if (event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP)
    {
        const auto *event =
            static_cast<ip_event_got_ip_t *>(event_data);

        sta_retry_count = 0;
        sta_connected = true;

        ESP_LOGI(
            TAG,
            "STA connected");

        ESP_LOGI(
            TAG,
            "STA IP: " IPSTR,
            IP2STR(&event->ip_info.ip));

        ESP_LOGI(
            TAG,
            "STA Gateway: " IPSTR,
            IP2STR(&event->ip_info.gw));

        ESP_LOGI(
            TAG,
            "STA Netmask: " IPSTR,
            IP2STR(&event->ip_info.netmask));

        return;
    }
}


// ============================================================
// Wi-Fi initialization
// ============================================================

esp_err_t wifi_manager_init()
{
    ESP_LOGI(
        TAG,
        "Initializing Wi-Fi manager...");

    // ========================================================
    // NVS
    // ========================================================

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(
            TAG,
            "NVS requires erase/reinitialization");

        err = nvs_flash_erase();

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "nvs_flash_erase() failed: %s",
                esp_err_to_name(err));

            return err;
        }

        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "NVS initialization failed: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // TCP/IP stack
    // ========================================================

    err = esp_netif_init();

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "esp_netif_init() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // Event loop
    // ========================================================

    err = esp_event_loop_create_default();

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "esp_event_loop_create_default() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // Create network interfaces
    // ========================================================

    sta_netif = esp_netif_create_default_wifi_sta();

    if (sta_netif == nullptr)
    {
        ESP_LOGE(
            TAG,
            "Failed to create STA network interface");

        return ESP_FAIL;
    }

    ap_netif = esp_netif_create_default_wifi_ap();

    if (ap_netif == nullptr)
    {
        ESP_LOGE(
            TAG,
            "Failed to create AP network interface");

        return ESP_FAIL;
    }

    // ========================================================
    // Initialize Wi-Fi driver
    // ========================================================

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&cfg);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_wifi_init() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // Register event handlers
    // ========================================================

    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        nullptr,
        &wifi_event_instance);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to register Wi-Fi event handler: %s",
            esp_err_to_name(err));

        return err;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        nullptr,
        &ip_event_instance);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to register IP event handler: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // STA configuration
    // ========================================================

    wifi_config_t sta_config{};

    std::strncpy(
        reinterpret_cast<char *>(sta_config.sta.ssid),
        WIFI_STA_SSID,
        sizeof(sta_config.sta.ssid));

    std::strncpy(
        reinterpret_cast<char *>(sta_config.sta.password),
        WIFI_STA_PASSWORD,
        sizeof(sta_config.sta.password));

    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // ========================================================
    // AP configuration
    // ========================================================

    wifi_config_t ap_config{};

    std::strncpy(
        reinterpret_cast<char *>(ap_config.ap.ssid),
        WIFI_AP_SSID,
        sizeof(ap_config.ap.ssid));

    std::strncpy(
        reinterpret_cast<char *>(ap_config.ap.password),
        WIFI_AP_PASSWORD,
        sizeof(ap_config.ap.password));

    ap_config.ap.ssid_len =
        std::strlen(WIFI_AP_SSID);

    ap_config.ap.channel =
        WIFI_AP_CHANNEL;

    ap_config.ap.max_connection =
        WIFI_AP_MAX_CONNECTIONS;

    ap_config.ap.authmode =
        WIFI_AUTH_WPA2_PSK;

    // ========================================================
    // Set AP+STA mode
    // ========================================================

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_wifi_set_mode() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // Apply STA configuration
    // ========================================================

    err = esp_wifi_set_config(
        WIFI_IF_STA,
        &sta_config);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to configure STA: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // Apply AP configuration
    // ========================================================

    err = esp_wifi_set_config(
        WIFI_IF_AP,
        &ap_config);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to configure AP: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // Start Wi-Fi
    // ========================================================

    err = esp_wifi_start();

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_wifi_start() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    // ========================================================
    // SoftAP information
    // ========================================================

    ESP_LOGI(
        TAG,
        "SoftAP started");

    ESP_LOGI(
        TAG,
        "AP SSID: %s",
        WIFI_AP_SSID);

    ESP_LOGI(
        TAG,
        "AP password: %s",
        WIFI_AP_PASSWORD);

    ESP_LOGI(
        TAG,
        "AP IP: 192.168.4.1");

    // ========================================================
    // mDNS
    // ========================================================

    err = mdns_init();

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "mdns_init() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    err = mdns_hostname_set("robot");

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "mdns_hostname_set() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    err = mdns_instance_name_set(
        "BBU Companion Robot");

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "mdns_instance_name_set() failed: %s",
            esp_err_to_name(err));

        return err;
    }

    ESP_LOGI(
        TAG,
        "mDNS hostname: robot.local");

    ESP_LOGI(
        TAG,
        "Wi-Fi manager initialized");

    return ESP_OK;
}


// ============================================================
// STA status
// ============================================================

bool wifi_manager_is_sta_connected()
{
    return sta_connected;
}


// ============================================================
// AP IP
// ============================================================

const char *wifi_manager_get_ap_ip()
{
    return "192.168.4.1";
}