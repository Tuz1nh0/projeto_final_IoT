#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#define ONEWIRE_GPIO       GPIO_NUM_4

#define TAG                "Wi-Fi STA"
#define WIFI_SSID          "Arthursouza"
#define WIFI_PASSWORD      "Maria1979"
#define TAG_HTTP           "HTTP_CLIENT"
#define THINGSPEAK_KEY     "NSW0NBGCFQS8EJK5"

#define WIFI_CONNECTED_BIT BIT0

//-----------------VARIABLES AND FUNCTIONS-----------------

static EventGroupHandle_t wifi_event_group;

static char *response_buffer = NULL;
static int response_len = 0;

void nvs_init();
void event_loop_init();
void wifi_init();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void http_get_request_task(void *pvParameters);
esp_err_t http_get_request_event_handler(esp_http_client_event_t *evt);

//-------------------------MAIN----------------------------

void app_main(void) {

	wifi_event_group = xEventGroupCreate();

	nvs_init();
	event_loop_init();
	wifi_init();

	gpio_config_t io_config = {
		.pin_bit_mask = (1ULL << ONEWIRE_GPIO),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	gpio_config(&io_config);

	xTaskCreate(&http_get_request_task, "http_get_request_task", 4096, NULL, 5, NULL);
      
}

//---------------DS18B20 1-WIRE PROTOCOL------------------

static void onewire_drive_low(void) {
	
	gpio_set_direction(ONEWIRE_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(ONEWIRE_GPIO, 0);

}

static void onewire_release(void) {
	
	gpio_set_direction(ONEWIRE_GPIO, GPIO_MODE_INPUT);

}

static bool onewire_reset_pulse(void) {

	onewire_drive_low();
	esp_rom_delay_us(480);
	onewire_release();
	esp_rom_delay_us(70);
	int presence = gpio_get_level(ONEWIRE_GPIO);
	esp_rom_delay_us(410);
	return(presence == 0);

}

static void onewire_write_bit(int bit) {
	
	onewire_drive_low();
	
	if(bit) {
		esp_rom_delay_us(6);
	}
	else {
		esp_rom_delay_us(60);
	}

	onewire_release();
	
	if(bit) {
		esp_rom_delay_us(64);
	}
	else {
		esp_rom_delay_us(10);
	}

}

static int onewire_read_bit(void) {

	int bit;
	onewire_drive_low();
	esp_rom_delay_us(6);
	onewire_release();
	esp_rom_delay_us(9);
	bit = gpio_get_level(ONEWIRE_GPIO);
	esp_rom_delay_us(55);
	return bit;

}

static void onewire_write_byte(uint8_t data) {

	for(int i = 0; i < 8; i++) {
		onewire_write_bit((data >> i) & 0x01);
	}

}

static uint8_t onewire_read_byte(void) {

	uint8_t value = 0;
	for(int i = 0; i < 8; i++) {
		value |= (onewire_read_bit() << i);
	}
	return value;

}

static esp_err_t ds18b20_read_temperature(float *temperature) {

	if(!onewire_reset_pulse()) {
		return ESP_FAIL;
	}
	
	onewire_write_byte(0xCC);
	onewire_write_byte(0x44);
	vTaskDelay(pdMS_TO_TICKS(750));
	
	if(!onewire_reset_pulse()) {
		return ESP_FAIL;
	}

	onewire_write_byte(0xCC);
	onewire_write_byte(0xBE);
	uint8_t lsb = onewire_read_byte();
	uint8_t msb = onewire_read_byte();

	for (int i = 0; i < 7; i++) {
		onewire_read_byte();
	}
	
	int16_t raw = (msb << 8) | lsb;
	*temperature = raw*0.0625f;
	return ESP_OK;
	
}

//------------------------Wi-Fi and ThingSpeak------------------------

void nvs_init() {

	esp_err_t ret = nvs_flash_init();
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret ==ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		ret = nvs_flash_init();
	}

}

void event_loop_init() {

	esp_netif_init();
	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();
	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
	esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
	
}

void wifi_init() {

	wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&wifi_cfg);
	esp_wifi_set_mode(WIFI_MODE_STA);

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = WIFI_SSID,
			.password = WIFI_PASSWORD
		},
	};

	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	esp_wifi_start();

}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {

	if(event_base == WIFI_EVENT) {
		switch(event_id) {
			case WIFI_EVENT_STA_START: {
				ESP_LOGI(TAG, "Wi-Fi started. Connecting...");
				esp_wifi_connect();
				break;
			};

			case WIFI_EVENT_STA_DISCONNECTED: {
				ESP_LOGW(TAG, "Wi-Fi disconnected. Trying to reconnect...");
				esp_wifi_connect();
				break;
			};

			default: {	
				ESP_LOGI(TAG, "Wi-Fi event not processed: %d", (int) event_id);
				break;
			}
		}
	}

	else if(event_base ==IP_EVENT) {
		switch(event_id) {
			case IP_EVENT_STA_GOT_IP: {
				ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
				ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
				xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
				break;
			};

			default: {
				break;
			}
		}
	}

}

void http_get_request_task(void *pvParameters) {
	
	float ds18b20_temperature = 0.0;

	while(1) {
		xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
		
		esp_err_t ds18b20_err = ds18b20_read_temperature(&ds18b20_temperature);

		if(ds18b20_err == ESP_OK) {
			char url[256];
			snprintf(url, sizeof(url), "http://api.thingspeak.com/update?api_key=%s&field1=%.2f", THINGSPEAK_KEY, ds18b20_temperature);
			esp_http_client_config_t config = {
				.url = url,
				.method = HTTP_METHOD_GET,
				.event_handler = http_get_request_event_handler,
				.disable_auto_redirect = true,
				.timeout_ms = 5000,
			};

			esp_http_client_handle_t client = esp_http_client_init(&config);

			esp_err_t err = esp_http_client_perform(client);

			if(err != ESP_OK) {
				ESP_LOGE(TAG_HTTP, "Request error: %s", esp_err_to_name(err));
			}

			esp_http_client_cleanup(client);
			vTaskDelay(pdMS_TO_TICKS(15000));
		};
	}

}

esp_err_t http_get_request_event_handler(esp_http_client_event_t *evt) {
	
	switch(evt->event_id) {
		case HTTP_EVENT_ON_DATA:
			if(evt->data && evt->data_len > 0) {
				char *new_buf = realloc(response_buffer, response_len + evt->data_len + 1);
				if(!new_buf) {
					ESP_LOGE(TAG_HTTP, "Fail to rellocate buffer");
					free(response_buffer);
					response_buffer = NULL;
					response_len = 0;
					return ESP_FAIL;
				}
				response_buffer = new_buf;
				memcpy(response_buffer + response_len, evt->data, evt->data_len);
				response_len += evt->data_len;
				response_buffer[response_len] = '\0';
			};
			break;

		case HTTP_EVENT_ON_FINISH:
			if(response_buffer) {
				ESP_LOGI(TAG_HTTP, "Received response (%d bytes):\n%s", response_len, response_buffer);
				free(response_buffer);
				response_buffer = NULL;
				response_len = 0;
			};
			break;

		case HTTP_EVENT_DISCONNECTED:

		case HTTP_EVENT_ERROR:
			if(response_buffer) {
				free(response_buffer);
				response_buffer = NULL;
				response_len = 0;
				ESP_LOGW(TAG_HTTP, "Request finished with error or disconnection");
			}
			break;

		default:
			break;
	}
	return ESP_OK;

}
