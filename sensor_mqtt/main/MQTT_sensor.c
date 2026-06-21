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
#include "mqtt_client.h"
#include "mqtt_secrets.h"

#define ONEWIRE_GPIO       GPIO_NUM_4

#define TAG_WIFI           "Wi-Fi STA"
#define WIFI_SSID          "ID"
#define WIFI_PASSWORD      "Senha"
#define TAG_HTTP           "HTTP_CLIENT
#define THINGSPEAK_CHANNEL "3407435"

#define WIFI_CONNECTED_BIT BIT0

//-----------------VARIABLES AND FUNCTIONS-----------------

static const char *TAG_MQTT = "MQTT_TCP";

static EventGroupHandle_t wifi_event_group;

void nvs_init();
void event_loop_init();
void wifi_init();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void mqtt_app_start(void);
static void mqtt_task(void *pvParameters);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

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

	mqtt_app_start();
	xTaskCreate(mqtt_task, "mqtt_task", 4096, NULL, 5, NULL);
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
				ESP_LOGI(TAG_WIFI, "Wi-Fi started. Connecting...");
				esp_wifi_connect();
				break;
			};

			case WIFI_EVENT_STA_DISCONNECTED: {
				ESP_LOGW(TAG_WIFI, "Wi-Fi disconnected. Trying to reconnect...");
				esp_wifi_connect();
				break;
			};

			default: {	
				ESP_LOGI(TAG_WIFI, "Wi-Fi event not processed: %d", (int) event_id);
				break;
			}
		}
	}

	else if(event_base ==IP_EVENT) {
		switch(event_id) {
			case IP_EVENT_STA_GOT_IP: {
				ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
				ESP_LOGI(TAG_WIFI, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
				xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
				break;
			};

			default: {
				break;
			}
		}
	}

}

static void mqtt_app_start(void) {
	
	esp_mqtt_client_config mqtt_cfg = {
		.uri = "",
		.credentials.client_id = SECRET_MQTT_CLIENT_ID,
		.credentials.username = SECRET_MQTT_USERNAME,
		.credentials.authentication.password = SECRET_MQTT_PASSWORD
	};

	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
	esp_mqtt_client_start(client);

}

static void mqtt_task(void *pvParameters) {

	float ds18b20_temperature = 0.0;
	char payload[64];
	char topic[128];

	snprintf(topic, sizeof(topic), "channels/%s/publish", THINGSPEAK_CHANNEL);

	while(1) {
		xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
		
		esp_err_t ds18b20_err = ds18b20_read_temperature(&ds18b20_temperature);

		if(ds18b20_err == ESP_OK) {
			ESP_LOGI("SENSOR", "Read temperature: %.2f °C", temperature);

			snprintf(payload, sizeof(payload), "field1=%2f", temperature);

			if (mqtt_client != NULL) {
				int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
				ESP_LOGI("MQTT", "Publishing [%s] on topic [%s]. Msg_id=%d", payload, topic, msg_id);
			}
		}
		else {
			ESP_LOGE("SENSOR", "Fail to read temperature");
		}

		vTaskDelay(pdMS_TO_TICKS(20000))
	}

}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {

	switch((esp_mqtt_event_id)event_id) {
		case MQTT_EVEBT_CONNETCTED:
			ESP_LOGI("MQTT", "Connected to ThingSpeak MQTT broker!");
			break;

		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI("MQTT", "Disconnected from ThingSpeak MQTT broker!");
			break;
	
		case MQTT_EVENT_ERROR:
			ESP_LOGI("MQTT", "ThingSpeak MQTT broker error!");
			break;

		default:
			break;
	}

}
