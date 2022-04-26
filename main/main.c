/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "esp_http_client.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "soc/adc_channel.h" // macros de GPIO a CHANNEL

#include "dht.h"

/* DEFINICIONES */

/* DIGITAL */

#define PIN_DHT11 CONFIG_PIN_DHT11

/* ANALOGICO */

#define NO_OF_SAMPLES   64          //Multisampling
#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t mq2_channel = ADC1_GPIO34_CHANNEL; // Channel 6 // GPIO34 if ADC1, GPIO14 if ADC2
static const adc_channel_t mq3_channel = ADC1_GPIO35_CHANNEL; // Channel 7 // GPIO35
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;

/*
* attenuation
* esp32 usa la atenuación para determinar cuanto puede leer de un valor analógico
* en este caso ponemos el valor máximo ya que estamos trabajando con valores analógicos
* que van desde 0v hasta 3.3v
*/
static const adc_atten_t atten = ADC_ATTEN_DB_11; // ADC_ATTEN_DB_6
/*
* unit
* esp32 tiene dos unidades de ADC la 1 y la 2
* La unidad 2 está deshabilitada si se está usando el WIFI
* por lo que vamos a usar la ADC_UNIT_1
* La primera unidad tiene disponibles 8 pines que van desde: GPIO32 - GPIO39
*/
static const adc_unit_t unit = ADC_UNIT_1;


/* HTTP */

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

/*
* Host y path de thingspeak que nos indica en su página web
* El WRITE_API_KEY es la key que nos proporciona thingspeak en su página web
*/
#define HOST "api.thingspeak.com"
#define PATH "/update" // "/update.json" ???
#define WRITE_API_KEY "JNQZ81RODX5LATC5" // CONFIG_WRITE_API_KEY // TODO: Quitar la api-key de aqui y llevarla a la configuración

static char query_str[128] = {0}; // buffer de caracteres que guardará la query a enviar

/* NOMBRE APP */

static const char *TAG = "Mota Sensora";

/* DEFINICIONES */





esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	static char *output_buffer;  // Buffer to store response of http request from event handler
	static int output_len;       // Stores number of bytes read
	switch(evt->event_id) {
		case HTTP_EVENT_ERROR:
			ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
			break;
		case HTTP_EVENT_ON_CONNECTED:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
			break;
		case HTTP_EVENT_HEADER_SENT:
			ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
			break;
		case HTTP_EVENT_ON_HEADER:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
			break;
		case HTTP_EVENT_ON_DATA:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
			/*
			 *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
			 *  However, event handler can also be used in case chunked encoding is used.
			 */
			if (!esp_http_client_is_chunked_response(evt->client)) {
				// If user_data buffer is configured, copy the response into the buffer
				if (evt->user_data) {
					memcpy(evt->user_data + output_len, evt->data, evt->data_len);
				} else {
					if (output_buffer == NULL) {
						output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
						output_len = 0;
						if (output_buffer == NULL) {
							ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
							return ESP_FAIL;
						}
					}
					memcpy(output_buffer + output_len, evt->data, evt->data_len);
				}
				output_len += evt->data_len;
			}

			break;
		case HTTP_EVENT_ON_FINISH:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
			if (output_buffer != NULL) {
				// Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
				// ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
				free(output_buffer);
				output_buffer = NULL;
			}
			output_len = 0;
			break;
		case HTTP_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
			int mbedtls_err = 0;
			esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
			if (err != 0) {
				ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
				ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
			}
			if (output_buffer != NULL) {
				free(output_buffer);
				output_buffer = NULL;
			}
			output_len = 0;
			break;
		case HTTP_EVENT_REDIRECT:
			ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
			esp_http_client_set_header(evt->client, "From", "user@example.com");
			esp_http_client_set_header(evt->client, "Accept", "text/html");
			break;
	}
	return ESP_OK;
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
	if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
		printf("Characterized using Two Point Value\n");
	} else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
		printf("Characterized using eFuse Vref\n");
	} else {
		printf("Characterized using Default Vref\n");
	}
}

static void check_efuse(void)
{
#if CONFIG_IDF_TARGET_ESP32
	//Check if TP is burned into eFuse
	if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
		printf("eFuse Two Point: Supported\n");
	} else {
		printf("eFuse Two Point: NOT supported\n");
	}
	//Check Vref is burned into eFuse
	if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
		printf("eFuse Vref: Supported\n");
	} else {
		printf("eFuse Vref: NOT supported\n");
	}
#elif CONFIG_IDF_TARGET_ESP32S2
	if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
		printf("eFuse Two Point: Supported\n");
	} else {
		printf("Cannot retrieve eFuse Two Point calibration values. Default calibration values will be used.\n");
	}
#else
#error "This example is configured for ESP32/ESP32S2."
#endif
}

/*
 * Valores necesarios para enviar a thingspeak:
 * valor de temperatura de DHT11
 * valor de humedad de DHT11
 * valor de MQ-2
 * valor de MQ-3
*/
static void send_data_to_server(int temp_value, int humi_value,
			float mq2_value, float mq3_value)
{
	ESP_LOGI(TAG, "send_data_to_server");
	//char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

	// ponemos el buffer a 0
	memset(query_str, 0, sizeof(query_str));
	// Llamada de ejemplo https://api.thingspeak.com/update?api_key= <api-key> &field1=123&field2=123&field3=3.141516&field4=3.141516
	sprintf(query_str, "api_key=" WRITE_API_KEY "&field1=%d&field2=%d&field3=%.3f&field4=%.3f",
				temp_value, humi_value, mq2_value, mq3_value);

	/**
	 * NOTE: All the configuration parameters for http_client must be spefied either in URL or as host and path parameters.
	 * If host and path parameters are not set, query parameter will be ignored. In such cases,
	 * query parameter should be specified in URL.
	 *
	 * If URL as well as host and path parameters are specified, values of host and path will be considered.
	 */
	esp_http_client_config_t config = {
		.host = HOST,
		.path = PATH,
		.query = query_str,
		.event_handler = _http_event_handler,
		//.user_data = local_response_buffer,        // Pass address of local buffer to get response
		.disable_auto_redirect = true,
	};

	// iniciamos un cliente con su configuración
	esp_http_client_handle_t client = esp_http_client_init(&config);

	// GET
	// Lanzamos la llamada HTTP
	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
				esp_http_client_get_status_code(client),
				esp_http_client_get_content_length(client));
	} else {
		ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
	}

	esp_http_client_cleanup(client);
}

static void connect_to_wifi(void)
{
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	/* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
	 * Read "Establishing Wi-Fi or Ethernet Connection" section in
	 * examples/protocols/README.md for more information about this function.
	 */
	ESP_ERROR_CHECK(example_connect());
	ESP_LOGI(TAG, "Connected to AP, begin http example");
}

static void config_ADC()
{
	//Check if Two Point or Vref are burned into eFuse
	check_efuse();

	//Configure ADC
	if (unit == ADC_UNIT_1) {
		adc1_config_width(width);
		adc1_config_channel_atten(mq2_channel, atten);
		adc1_config_channel_atten(mq3_channel, atten);
	}

	//Characterize ADC
	adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
	esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
	print_char_val_type(val_type);
}

static int read_from_dht11(struct dht_reading * data)
{
	struct dht_reading new_data;

	new_data = DHT_read(); // solo lee cada 2 segundos
	if (new_data.status == DHT_OK) {
		*data = new_data; // solo actualiza si ha obtenido los datos correctamente
	}

	return new_data.status;
}

void app_main(void)
{
	connect_to_wifi();
	//xTaskCreate(&http_test_task, "http_test_task", 8192, NULL, 5, NULL);

	/* Config sensores analógicos */
	config_ADC();

	/* Init DHT11 sensor */
	DHT11_init(PIN_DHT11);

	struct dht_reading dht11_data = {0};
	//float RS_air;
	//float R0;

	while(1) {
#if 0
/* Obtención de los datos */
		uint32_t adc_reading = 0;
		//Multisampling
		for (int i = 0; i < NO_OF_SAMPLES; i++) {
			if (unit == ADC_UNIT_1) {
				adc_reading += adc1_get_raw((adc1_channel_t)channel);
			} else {
				int raw;
				adc2_get_raw((adc2_channel_t)channel, width, &raw);
				adc_reading += raw;
			}
		}
		adc_reading /= NO_OF_SAMPLES;

		//Convert adc_reading to voltage in mV
		uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
		printf("Raw: %d\tVoltage: %dmV\n", adc_reading, voltage);

		float RS_air = (float)(2450 - voltage) / (float)(voltage);
		//R0 = RS_air/(float)10.0;

		//printf("Rs_air %f R0 %f ratio %f\n", (float)voltage, voltage/10.f, (float)voltage/(voltage/10.f));


#endif
		/* Sensor DHT11 */
		read_from_dht11(&dht11_data);

/* Obtención de los datos */

		/* Envio de datos */
		send_data_to_server(dht11_data.temperature, dht11_data.humidity,
				0.f, 0.f);
		/* Envio de datos */

		// Para thingspeak free tenemos que esperar 15s entre llamada y llamada
		vTaskDelay(pdMS_TO_TICKS(15500));
	}
	ESP_LOGI(TAG, "Fin appmain");
}
