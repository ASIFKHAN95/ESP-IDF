#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "Industrial_IoT";

#define BLINK_GPIO CONFIG_BLINK_GPIO

// Handle for our addressable RGB LED
static led_strip_handle_t led_strip;

// Handle for our FreeRTOS Queue
static QueueHandle_t sensor_queue;

/* --- STEP 1: INITIALIZATION --- */
static void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring hardware addressable LED...");
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, 
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, 
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#endif
    led_strip_clear(led_strip);
}

/* --- STEP 2: THE SENSOR TASK (Producer) --- */
// This simulates an Air Quality sensor reading data every 2 seconds.
void vSensorTask(void *pvParameters)
{
    int simulated_aqi = 50; // Start with healthy air quality

    while(1)
    {
        // Simulate reading a sensor. Let's slowly degrade air quality
        simulated_aqi += (rand() % 15) - 5; 
        
        ESP_LOGI(TAG, "[Sensor Task] Read simulated AQI: %d", simulated_aqi);

        // Send the sensor reading to the queue. 
        // If the queue is full, wait up to 100 milliseconds for space.
        if (xQueueSend(sensor_queue, &simulated_aqi, pdMS_TO_TICKS(100)) != pdPASS) {
            ESP_LOGW(TAG, "[Sensor Task] Failed to send data! Queue full.");
        }

        // Wait 2 seconds before reading again
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* --- STEP 3: THE LED TASK (Consumer) --- */
// This task waits for data to arrive in the queue and updates the hardware.
void vLedTask(void *pvParameters)
{
    int received_aqi = 0;

    while(1)
    {
        // Block and wait forever until something arrives in the queue
        if (xQueueReceive(sensor_queue, &received_aqi, portMAX_DELAY) == pdPASS) {
            
            ESP_LOGI(TAG, "[LED Task] Received AQI update: %d", received_aqi);

            // Let's change colors based on Air Quality
            if (received_aqi < 70) {
                // GOOD: Green
                ESP_LOGI(TAG, "Air is Good. Setting LED to Green.");
                led_strip_set_pixel(led_strip, 0, 0, 100, 0); 
            } else if (received_aqi >= 70 && received_aqi < 100) {
                // WARNING: Yellow (Red + Green)
                ESP_LOGI(TAG, "Air is Moderate. Setting LED to Yellow.");
                led_strip_set_pixel(led_strip, 0, 50, 50, 0);
            } else {
                // DANGER: Red
                ESP_LOGE(TAG, "Air is Toxic! Setting LED to Red.");
                led_strip_set_pixel(led_strip, 0, 100, 0, 0);
            }

            led_strip_refresh(led_strip);
        }
    }
}

/* --- STEP 4: APP MAIN --- */
void app_main(void)
{
    configure_led();

    // 1. Create a queue that can hold 5 integers
    sensor_queue = xQueueCreate(5, sizeof(int));
    if (sensor_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue! System halting.");
        return;
    }

    // 2. Create the Sensor Task
    xTaskCreate(
        vSensorTask,        // Function that implements the task
        "Sensor_Reader",    // Name of the task for debugging
        2048,               // Stack size in bytes
        NULL,               // Parameter to pass to the task
        2,                  // Priority (higher number = higher priority)
        NULL                // Task handle
    );

    // 3. Create the LED Task
    xTaskCreate(
        vLedTask,           // Function that implements the task
        "LED_Controller",   // Name of the task
        2048,               // Stack size
        NULL,               // Parameter
        1,                  // Lower priority than the sensor task
        NULL                // Task handle
    );

    ESP_LOGI(TAG, "Multi-tasking system started successfully!");
}