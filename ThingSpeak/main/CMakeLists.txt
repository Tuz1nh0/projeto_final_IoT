idf_component_register(SRCS "HTTP_sensor.c"
                    INCLUDE_DIRS "."
		    		PRIV_REQUIRES 
		        		nvs_flash 
						esp_wifi 
						esp_http_client 
						spi_flash
						esp_driver_gpio)						
