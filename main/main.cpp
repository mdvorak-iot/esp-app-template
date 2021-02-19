#include <aws_shadow.h>
#include <double_reset.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <mqtt_client.h>
#include <nvs_flash.h>
#include <status_led.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <wifi_reconnect.h>

#define APP_WIFI_PROV_TIMEOUT_S CONFIG_APP_WIFI_PROV_TIMEOUT_S

static const char TAG[] = "main";

static bool reconfigure = false;
static bool mqtt_started = true;
static esp_mqtt_client_handle_t mqtt_client = nullptr;
static aws_shadow_handle_t shadow_client = nullptr;
static esp_timer_handle_t provisioning_timer = nullptr;

static void wifi_sta_start()
{
    // Start standard wifi mode
    ESP_LOGI(TAG, "starting wifi in sta mode");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Connect now
    wifi_reconnect_resume();
}

static void provisioning_timer_delete()
{
    if (provisioning_timer)
    {
        esp_timer_stop(provisioning_timer);
        esp_timer_delete(provisioning_timer);
        provisioning_timer = nullptr;
    }
}

static void provisioning_timer_handler(void *arg)
{
    ESP_LOGI(TAG, "provisioning timeout");
    wifi_prov_mgr_stop_provisioning();
    wifi_prov_mgr_deinit();
    wifi_sta_start();

    // Cleanup
    provisioning_timer_delete();
}

static void do_mqtt_connect()
{
    if (!mqtt_started)
    {
        // Initial connection
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_start(mqtt_client));
        mqtt_started = true;
    }
    else
    {
        // Ignore error here
        esp_mqtt_client_reconnect(mqtt_client);
    }
}

static void wifi_prov_event_handler(__unused void *arg, __unused esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "provisioning started");
        status_led_set_interval(STATUS_LED_DEFAULT, 50, true);
        break;
    case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
        ESP_LOGI(TAG, "provisioning received ssid '%s'", (const char *)wifi_sta_cfg->ssid);
        provisioning_timer_delete(); // Stop timer now, so it does not kill provisioning in progress
        break;
    }
    case WIFI_PROV_CRED_FAIL: {
        wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
        ESP_LOGE(TAG, "provisioning failed: %s", (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi STA authentication failed" : "Wi-Fi AP not found");
        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "provisioning successful");
        break;
    case WIFI_PROV_END:
        ESP_LOGD(TAG, "provisioning end");
        wifi_prov_mgr_deinit();
        provisioning_timer_delete();

        // Start reconnecting, it can use old credentials, even when provisioning failed (if available)
        wifi_reconnect_resume();
        break;
    default:
        break;
    }
}

static void setup_init()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Check double reset
    // NOTE this should be called as soon as possible, ideally right after nvs init
    ESP_ERROR_CHECK(double_reset_start(&reconfigure, DOUBLE_RESET_DEFAULT_TIMEOUT));

    // Status LED
    ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_create_default());
    ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_set_interval(STATUS_LED_DEFAULT, 500, true));

    // Events
    esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, [](void *, esp_event_base_t, int32_t, void *) { status_led_set_interval(STATUS_LED_DEFAULT, 500, true); }, nullptr);
    esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, [](void *, esp_event_base_t, int32_t, void *) { status_led_set_interval_for(STATUS_LED_DEFAULT, 200, false, 700, true); }, nullptr);

    // MQTT
    //    esp_mqtt_client_config_t mqtt_cfg = {};
    //    mqtt_cfg.host = CONFIG_AWS_IOT_MQTT_HOST;
    //    mqtt_cfg.port = CONFIG_AWS_IOT_MQTT_PORT;
    //    mqtt_cfg.client_id = CONFIG_AWS_IOT_MQTT_CLIENT_ID;
    //    mqtt_cfg.transport = MQTT_TRANSPORT_OVER_SSL;
    //    mqtt_cfg.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    //    mqtt_cfg.cert_pem = (const char *)aws_root_ca_pem_start;
    //    mqtt_cfg.client_cert_pem = (const char *)certificate_pem_crt_start;
    //    mqtt_cfg.client_key_pem = (const char *)private_pem_key_start;
    //
    //    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    //    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL));
    //
    //    esp_event_handler_instance_register(
    //        IP_EVENT, IP_EVENT_STA_GOT_IP, [](void *, esp_event_base_t, int32_t, void *) { do_mqtt_connect(); },
    //        nullptr, nullptr);
    //
    //    // Shadow
    //    ESP_ERROR_CHECK(aws_shadow_init(mqtt_client, CONFIG_AWS_IOT_THING_NAME, NULL, &shadow_client));
    //    ESP_ERROR_CHECK(aws_shadow_handler_register(shadow_client, AWS_SHADOW_EVENT_ANY, shadow_event_handler, NULL));
}

static void setup_devices()
{
    // Custom devices and other init, that needs to be done before waiting for wifi connection
}

static void setup_wifi()
{
    // Get app info
    esp_app_desc_t app_info = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(esp_ota_get_running_partition(), &app_info));

    // Service device_name
    uint64_t mac = 0;
    ESP_ERROR_CHECK(esp_efuse_mac_get_default((uint8_t *)&mac));

    char device_name[33] = {}; // max 32 characters
    snprintf(device_name, sizeof(device_name), "%.25s-%06llx", app_info.project_name, mac);
    ESP_LOGI(TAG, "device name '%s'", device_name);

    // Initalize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, device_name));
    ESP_ERROR_CHECK(wifi_reconnect_start()); // NOTE this must be called before connect, otherwise it might miss connected event

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, nullptr));
    wifi_prov_mgr_config_t wifi_prof_cfg = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(wifi_prof_cfg));

    // Provisioning mode
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned || reconfigure)
    {
        // Provisioning mode
        ESP_LOGI(TAG, "provisioning starting");

        char service_name[sizeof(device_name) + 5] = {};
        snprintf(service_name, sizeof(service_name), "PROV_%s", device_name);

        //ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_create("custom-data")); // TODO
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, nullptr, service_name, nullptr));

        esp_timer_create_args_t args = {
            .callback = provisioning_timer_handler,
            .name = "wifi_prov_timer",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &provisioning_timer));
        ESP_ERROR_CHECK(esp_timer_start_once(provisioning_timer, APP_WIFI_PROV_TIMEOUT_S * 1000000));
    }
    else
    {
        // Deallocate wifi provisioning
        wifi_prov_mgr_deinit();

        // Start standard wifi mode
        wifi_sta_start();
    }
}

static void setup_final()
{
    // Ready
    esp_app_desc_t app_info = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(esp_ota_get_running_partition(), &app_info));
    ESP_LOGI(TAG, "started %s %s", app_info.project_name, app_info.version);
}

static void run()
{
}

extern "C" void app_main()
{
    // Setup
    setup_init();
    setup_devices();
    setup_wifi();
    setup_final();

    // Run
    run();
}
