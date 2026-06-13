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

#define ONEWIRE_GPIO GPIO_NUM_4

//-------------------------MAIN----------------------------

void app_main(void) {



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

static void onewire_read_bit(void) {

	int bit;
	onewire_drive_low();
	esp_rom_delay_us(6);
	one_wire_release();
	esp_rom_delay_us(9);
	bit = gpio_get_leve(ONEWIRE_GPIO);
	esp_rom_delay_us(ONEWIRE_GPIO);
	return bit;

}

static void one_wire_write_byte(uint8_t data) {

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
	onewire_write_byte(Ox44);
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

//-------------------------MQTT----------------------------



