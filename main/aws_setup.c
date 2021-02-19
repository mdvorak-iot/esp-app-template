#include <esp_log.h>
#include <wifi_provisioning/manager.h>

static const char TAG[] = "aws_setup";

static esp_err_t aws_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                       uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf)
    {
        // TODO debug only
        ESP_LOGI(TAG, "received data: %.*s", inlen, (const char *)inbuf);
    }

    *outlen = 0;
    return ESP_OK;
}

static void aws_prov_event_handler(__unused void *arg, __unused esp_event_base_t event_base, int32_t event_id, __unused void *event_data)
{
    switch (event_id)
    {
    case WIFI_PROV_INIT:
        ESP_LOGI(TAG, "creating endpoint");
        ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_create("aws"));
        break;
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "registering endpoint");
        ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_register("aws", aws_prov_data_handler, NULL));
        break;
    default:
        break;
    }
}

void setup_aws()
{
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &aws_prov_event_handler, NULL));
}
