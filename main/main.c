#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define MQTT_URI "mqtt://host.wokwi.internal:1883"
#define MQTT_TOPIC "/emohtrams9191/SMART_HOME"
#define MQTT_TEMP_TOPIC "/emohtrams9191/temp"
#define MQTT_HUMIDITY_TOPIC "/emohtrams9191/humidity"

#define DHT_PIN 15
#define BUTTON_PIN 25
#define TRIG_PIN 27
#define ECHO_PIN 26
#define LED_TV 23
#define LDR_PIN 2
#define PIR_PIN 4
#define LED_TOILET 18
#define LED_LIVING 19
#define LED_BEDROOM 5
#define ALARM_PIN 22
#define SERVO_DOOR 32
#define SERVO_CURTAIN 33
#define SERVO_AC 21
#define SERVO_FAN 12
#define SERVO_PUMP 13

#define WIFI_CONNECTED_BIT BIT0
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_MODE LEDC_LOW_SPEED_MODE
#define SERVO_FREQUENCY 50
#define SERVO_RESOLUTION LEDC_TIMER_13_BIT

static const char *TAG = "HOME_IOT";
static EventGroupHandle_t wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client;
static volatile bool mqtt_pump_state;
static volatile int pending_command;
static bool sensor_pump_state;

static void publish(const char *topic, const char *payload)
{
    if (mqtt_client != NULL) {
        esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() != NULL ? ESP_OK : ESP_FAIL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t config = {0};
    strcpy((char *)config.sta.ssid, WIFI_SSID);
    strcpy((char *)config.sta.password, WIFI_PASSWORD);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.channel = 6;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        portMAX_DELAY);
}

static void mqtt_event_handler(void *args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC, 0);
    } else if (event_id == MQTT_EVENT_DATA) {
        char payload[16] = {0};
        size_t length = event->data_len < sizeof(payload) - 1 ? event->data_len : sizeof(payload) - 1;
        memcpy(payload, event->data, length);
        int command = atoi(payload);
        switch (command) {
        case 1: gpio_set_level(LED_TV, 1); break;
        case -1: gpio_set_level(LED_TV, 0); break;
        case 2: gpio_set_level(LED_LIVING, 1); break;
        case -2: gpio_set_level(LED_LIVING, 0); break;
        case 3: /* handled by the device task */ break;
        case -3: break;
        case 4: break;
        case -4: break;
        case 5: gpio_set_level(LED_BEDROOM, 1); break;
        case -5: gpio_set_level(LED_BEDROOM, 0); break;
        case 6: mqtt_pump_state = true; break;
        case -6: mqtt_pump_state = false; break;
        default: break;
        }
        if (command == 3 || command == -3 || command == 4 || command == -4) {
            pending_command = command;
        }
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "MQTT disconnected");
    }
}

static void mqtt_start(void)
{
    const esp_mqtt_client_config_t config = {.broker.address.uri = MQTT_URI};
    mqtt_client = esp_mqtt_client_init(&config);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

static void gpio_init_all(void)
{
    const uint64_t outputs = (1ULL << LED_TV) | (1ULL << LED_TOILET) |
        (1ULL << LED_LIVING) | (1ULL << LED_BEDROOM) | (1ULL << ALARM_PIN) |
        (1ULL << TRIG_PIN);
    gpio_config_t output_config = {
        .pin_bit_mask = outputs, .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&output_config));

    const uint64_t inputs = (1ULL << LDR_PIN) | (1ULL << PIR_PIN) |
        (1ULL << ECHO_PIN) | (1ULL << BUTTON_PIN);
    gpio_config_t input_config = {
        .pin_bit_mask = inputs, .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&input_config));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY));
}

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = SERVO_MODE, .timer_num = SERVO_TIMER,
        .duty_resolution = SERVO_RESOLUTION, .freq_hz = SERVO_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const int pins[] = {SERVO_DOOR, SERVO_CURTAIN, SERVO_AC, SERVO_FAN, SERVO_PUMP};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        ledc_channel_config_t channel = {
            .gpio_num = pins[i], .speed_mode = SERVO_MODE,
            .channel = (ledc_channel_t)i, .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_TIMER, .duty = 0, .hpoint = 0};
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
    }
}

static void servo_write(unsigned channel, int angle)
{
    const uint32_t min_duty = 410;  // 0.5 ms at 50 Hz, 13-bit resolution
    const uint32_t max_duty = 1966; // 2.4 ms at 50 Hz
    uint32_t duty = min_duty + (uint32_t)((max_duty - min_duty) * angle / 180);
    ESP_ERROR_CHECK(ledc_set_duty(SERVO_MODE, (ledc_channel_t)channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_MODE, (ledc_channel_t)channel));
}

static bool wait_for_gpio_level(gpio_num_t pin, int level, uint32_t timeout_us)
{
    int64_t deadline = esp_timer_get_time() + timeout_us;
    while (gpio_get_level(pin) != level) {
        if (esp_timer_get_time() >= deadline) {
            return false;
        }
        esp_rom_delay_us(2);
    }
    return true;
}

static bool dht_read(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT_PIN, 1);
    esp_rom_delay_us(40);
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);
    if (!wait_for_gpio_level(DHT_PIN, 0, 1000) ||
        !wait_for_gpio_level(DHT_PIN, 1, 1000) ||
        !wait_for_gpio_level(DHT_PIN, 0, 1000)) return false;
    for (int bit = 0; bit < 40; ++bit) {
        if (!wait_for_gpio_level(DHT_PIN, 1, 1000)) return false;
        esp_rom_delay_us(40);
        data[bit / 8] = (data[bit / 8] << 1) | gpio_get_level(DHT_PIN);
        if (!wait_for_gpio_level(DHT_PIN, 0, 1000)) return false;
    }
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) return false;
    *humidity = ((data[0] << 8) | data[1]) / 10.0f;
    *temperature = (((data[2] & 0x7f) << 8) | data[3]) / 10.0f;
    if (data[2] & 0x80) *temperature = -*temperature;
    return true;
}

static void device_task(void *arg)
{
    (void)arg;
    float previous_temperature = -1000, previous_humidity = -1000;
    int previous_pir = 0, previous_ldr = 0;
    bool previous_pump = false;
    while (true) {
        int command = pending_command;
        if (command != 0) {
            pending_command = 0;
            if (command == 3 || command == -3) {
                servo_write(2, command > 0 ? 180 : 0);
            } else {
                servo_write(0, command > 0 ? 180 : 0);
            }
        }
        float temperature, humidity;
        if (dht_read(&temperature, &humidity) &&
            (temperature != previous_temperature || humidity != previous_humidity)) {
            char value[16];
            snprintf(value, sizeof(value), "%.2f", temperature);
            publish(MQTT_TEMP_TOPIC, value);
            snprintf(value, sizeof(value), "%.2f", humidity);
            publish(MQTT_HUMIDITY_TOPIC, value);
            gpio_set_level(ALARM_PIN, temperature > 50);
            servo_write(3, temperature > 28 ? 180 : 0);
            previous_temperature = temperature;
            previous_humidity = humidity;
        }
        int pir = gpio_get_level(PIR_PIN);
        if (pir != previous_pir) {
            gpio_set_level(LED_TOILET, pir);
            previous_pir = pir;
        }
        int ldr = gpio_get_level(LDR_PIN);
        if (ldr != previous_ldr) {
            servo_write(1, ldr ? 0 : 180);
            publish(MQTT_TOPIC, ldr ? "-7" : "7");
            previous_ldr = ldr;
        }
        gpio_set_level(TRIG_PIN, 0);
        esp_rom_delay_us(2);
        gpio_set_level(TRIG_PIN, 1);
        esp_rom_delay_us(10);
        gpio_set_level(TRIG_PIN, 0);
        bool echo_started = wait_for_gpio_level(ECHO_PIN, 1, 30000);
        int64_t echo_start = esp_timer_get_time();
        bool echo_finished = echo_started &&
            wait_for_gpio_level(ECHO_PIN, 0, 30000);
        int distance_cm = echo_finished
            ? (int)((esp_timer_get_time() - echo_start) * 0.0344 / 2)
            : 0;
        sensor_pump_state = distance_cm > 150;

        bool pump = mqtt_pump_state && sensor_pump_state;
        if (pump != previous_pump) {
            servo_write(4, pump ? 180 : 0);
            publish(MQTT_TOPIC, pump ? "6" : "-6");
            previous_pump = pump;
        }
        if (gpio_get_level(BUTTON_PIN) == 0) {
            servo_write(0, 180);
            publish(MQTT_TOPIC, "4");
            vTaskDelay(pdMS_TO_TICKS(5000));
            servo_write(0, 0);
            publish(MQTT_TOPIC, "-4");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    gpio_init_all();
    servo_init();
    wifi_init_sta();
    mqtt_start();
    xTaskCreate(device_task, "device_task", 4096, NULL, 5, NULL);
}
