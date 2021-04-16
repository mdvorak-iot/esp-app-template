#include <aws_iot_shadow.h>
#include <esp_log.h>
#include <status_led.h>
#include <wps_config.h>

static const char TAG[] = "setup_status_led";

static const uint32_t STATUS_LED_CONNECTING_INTERVAL = 500u;
static const uint32_t STATUS_LED_PROV_INTERVAL = 100u;
static const uint32_t STATUS_LED_READY_INTERVAL = 100u;
static const uint32_t STATUS_LED_READY_COUNT = 3;

static void wps_event_handler(__unused void *handler_arg, esp_event_base_t event_base,
                              int32_t event_id, __unused void *event_data)
{
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;

    if (event_base == WIFI_EVENT
        && (event_id == WIFI_EVENT_STA_WPS_ER_SUCCESS || event_id == WIFI_EVENT_STA_WPS_ER_TIMEOUT || event_id == WIFI_EVENT_STA_WPS_ER_FAILED))
    {
        status_led_set_interval(status_led, STATUS_LED_CONNECTING_INTERVAL, true);
    }
    else if (event_base == WPS_CONFIG_EVENT && event_id == WPS_CONFIG_EVENT_START)
    {
        status_led_set_interval(status_led, STATUS_LED_PROV_INTERVAL, true);
    }
}

static void disconnected_handler(__unused void *handler_arg, __unused esp_event_base_t event_base,
                                 __unused int32_t event_id, __unused void *event_data)
{
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;
    status_led_set_interval(status_led, STATUS_LED_CONNECTING_INTERVAL, true);
}

static void shadow_ready_handler(__unused void *handler_arg, __unused esp_event_base_t event_base,
                                 __unused int32_t event_id, __unused void *event_data)
{
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;

    uint32_t timeout_ms = STATUS_LED_READY_INTERVAL * (STATUS_LED_READY_COUNT * 2 + 1);
    status_led_set_interval_for(status_led, STATUS_LED_READY_INTERVAL, false, timeout_ms, false);
}

void setup_status_led(gpio_num_t status_led_pin, bool status_led_on_state, aws_iot_shadow_handle_ptr shadow_handle, status_led_handle_ptr *out_status_led)
{
    // Status LED
    status_led_handle_ptr status_led = NULL;
    esp_err_t err = status_led_create(status_led_pin, status_led_on_state, &status_led);
    if (err == ESP_OK)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_set_interval(status_led, STATUS_LED_CONNECTING_INTERVAL, true));

        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wps_event_handler, status_led, NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WPS_CONFIG_EVENT, WPS_CONFIG_EVENT_START, wps_event_handler, status_led, NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, disconnected_handler, status_led, NULL));

        if (shadow_handle) // Don't log assert when shadow init failed
        {
            ESP_ERROR_CHECK_WITHOUT_ABORT(aws_iot_shadow_handler_instance_register(shadow_handle, AWS_IOT_SHADOW_EVENT_READY, shadow_ready_handler, status_led, NULL));
        }

        // Propagate result
        *out_status_led = status_led;
    }
    else
    {
        ESP_LOGE(TAG, "failed to create status_led on pin %d: %d %s", status_led_pin, err, esp_err_to_name(err));
    }
}