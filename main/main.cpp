#include <aws_shadow.h>
#include <double_reset.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <mqtt_client.h>
#include <nvs_flash.h>
#include <status_led.h>
#include <wifi_provisioning/manager.h>
#include <wifi_reconnect.h>

static const char TAG[] = "main";

static bool reconfigure = false;
static bool mqtt_started = true;
static esp_mqtt_client_handle_t mqtt_client = nullptr;
static aws_shadow_handle_t shadow_client = nullptr;

extern "C" void setup_wifi(bool reconfigure); // defined in wifi_setup.c

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
    esp_event_handler_register(
        WIFI_PROV_EVENT, WIFI_PROV_START, [](void *, esp_event_base_t, int32_t, void *) { status_led_set_interval(STATUS_LED_DEFAULT, 50, true); }, nullptr);
    esp_event_handler_register(
        WIFI_PROV_EVENT, WIFI_PROV_END, [](void *, esp_event_base_t, int32_t, void *) {
            if (!wifi_reconnect_is_connected())
            {
                status_led_set_interval(STATUS_LED_DEFAULT, 500, true);
            }
        },
        nullptr);

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
    setup_wifi(reconfigure);
    setup_final();

    // Run
    run();
}
