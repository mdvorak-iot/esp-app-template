#include "app_status.h"
#include <button.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <string.h>
#include <wifi_auto_prov.h>
#include <wifi_reconnect.h>

static const char TAG[] = "app_main";

#define APP_DEVICE_NAME CONFIG_APP_DEVICE_NAME
#define APP_CONTROL_BUTTON_PIN CONFIG_APP_CONTROL_BUTTON_PIN
#define APP_CONTROL_BUTTON_PROVISION_MS CONFIG_APP_CONTROL_BUTTON_PROVISION_MS
#define APP_CONTROL_BUTTON_FACTORY_RESET_MS CONFIG_APP_CONTROL_BUTTON_FACTORY_RESET_MS
#define APP_SNTP_SERVER CONFIG_APP_SNTP_SERVER

static RTC_DATA_ATTR bool force_provisioning = false;

static void app_disconnect()
{
    // NOTE disconnect from any services, e.g. MQTT server
    wifi_reconnect_pause();
    esp_wifi_disconnect();
    // Slight delay for any async processes to finish
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

static void control_button_handler(__unused void *arg, const struct button_data *data)
{
    if (data->event == BUTTON_EVENT_PRESSED && data->long_press)
    {
        // Factory reset
        ESP_LOGW(TAG, "user requested factory reset");
        app_disconnect();
        ESP_LOGW(TAG, "erase nvs");
        nvs_flash_deinit();
        nvs_flash_erase();
        esp_restart();
    }
    else if (data->event == BUTTON_EVENT_RELEASED && data->press_length_ms > APP_CONTROL_BUTTON_PROVISION_MS && !data->long_press)
    {
        // Provision Wi-Fi
        ESP_LOGW(TAG, "user requested wifi provisioning");
        app_disconnect();
        // Use RTC memory and deep sleep, to restart main cpu with provisioning flag set to true
        force_provisioning = true;
        esp_deep_sleep(1000);
    }
    else if (data->event == BUTTON_EVENT_RELEASED)
    {
        // App action
        ESP_LOGW(TAG, "user click");
        // TODO
    }
}

static void setup_sntp()
{
#if LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(1); // accept NTP offers from DHCP server, if any
#endif
#if LWIP_DHCP_GET_NTP_SRV && SNTP_MAX_SERVERS > 1
    sntp_setservername(1, APP_SNTP_SERVER);
#else
    // otherwise, use DNS address from a pool
    sntp_setservername(0, APP_SNTP_SERVER);
#endif

    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();
}

void setup()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // System services
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // Provisioning mode
    bool provision_now = force_provisioning;
    force_provisioning = false;

    // Setup control button
    struct button_config control_btn_cfg = {
        .level = BUTTON_LEVEL_LOW_ON_PRESS,
        .internal_pull = true,
        .long_press_ms = APP_CONTROL_BUTTON_FACTORY_RESET_MS,
        .continuous_callback = false,
        .on_press = control_button_handler,
        .on_release = control_button_handler,
        .arg = NULL,
    };
    ESP_ERROR_CHECK(button_config(APP_CONTROL_BUTTON_PIN, &control_btn_cfg, NULL));

    // Setup status LED
    app_status_init();

    // Setup Wi-Fi
    char device_name[WIFI_AUTO_PROV_SERVICE_NAME_LEN] = {};
    ESP_ERROR_CHECK(wifi_auto_prov_generate_name(device_name, sizeof(device_name), APP_DEVICE_NAME, false));

    struct wifi_auto_prov_config wifi_cfg = WIFI_AUTO_PROV_CONFIG_DEFAULT();
    wifi_cfg.service_name = device_name;
    wifi_cfg.wifi_connect = wifi_reconnect_resume;
    ESP_ERROR_CHECK(wifi_auto_prov_print_qr_code_handler_register(NULL));
    ESP_ERROR_CHECK(wifi_auto_prov_init(&wifi_cfg));
    ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, device_name));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(wifi_reconnect_start());

    // Setup SNTP
    setup_sntp();

    // Start
    ESP_ERROR_CHECK(wifi_auto_prov_start(provision_now));
}

_Noreturn void app_main()
{
    setup();

    // Run
    ESP_LOGI(TAG, "started");

    while (true)
    {
        vTaskDelay(1);
    }
}
