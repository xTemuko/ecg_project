#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "ecg_config.h"
#include "driver/gpio.h"


static const char *TAG_ADC = "ADC_Task";

// Función para escribir en el ADS1115 (registro de configuración)
static esp_err_t ads1115_write_config(uint8_t msb, uint8_t lsb) {
    uint8_t data[3] = { ADS1115_REG_CONFIG, msb, lsb };
    // Enviar 3 bytes: puntero de registro + 2 bytes de config
    return i2c_master_write_to_device(I2C_MASTER_NUM, ADS1115_ADDR, data, sizeof(data), 1000 / portTICK_PERIOD_MS);
}

// Función para leer el registro de conversión (16 bits) del ADS1115
static esp_err_t ads1115_read_conversion(int16_t *out_value) {
    // Enviar puntero del registro de conversión (0x00), luego leer 2 bytes
    uint8_t reg = ADS1115_REG_CONV;
    uint8_t buf[2];
    esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM, ADS1115_ADDR, &reg, 1, buf, 2, 1000 / portTICK_PERIOD_MS);
    if (err == ESP_OK) {
        // Combinar los dos bytes en un int16 (MSB primero)
        *out_value = ((int16_t)buf[0] << 8) | buf[1];
    }
    return err;
}


void configure_ad8233_sdn_pin(void) {
    gpio_config_t io_conf;
    // Deshabilitar interrupciones
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // Configurar como modo de salida
    io_conf.mode = GPIO_MODE_OUTPUT;
    // Máscara de bits de los pines a configurar (solo el pin 12)
    io_conf.pin_bit_mask = (1ULL << SDN_AD8233);
    // Deshabilitar pull-down
    io_conf.pull_down_en = 0;
    // Deshabilitar pull-up
    io_conf.pull_up_en = 0;
    // Aplicar la configuración
    gpio_config(&io_conf);
    ESP_LOGI("AD8233_CTRL", "Pin SDN (GPIO %d) configurado como salida.", SDN_AD8233);
}


void core1_adc_task(void *arg) {
    // Configurar ADS1115: modo continuo, AIN0, PGA ±4.096V, 475SPS, sin comparador
    // Valores de configuración (16 bits): 0x42C3 
    // - OS=0 (start conversion), MUX=100 (AIN0-GND), PGA=001 (4.096V), MODE=0 (cont)
    // - DR=110 (475 SPS), COMP_MODE=0, COMP_POL=0, COMP_LAT=0, COMP_QUE=11 (disable)
    esp_err_t err = ads1115_write_config(0x42, 0xC3);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ADC, "Error config ADS1115 (0x42C3): %d", err);
    } else {
        ESP_LOGI(TAG_ADC, "ADS1115 configurado en modo continuo 475SPS");
    }

    // Variables de muestreo
    int buffer_index;
    int16_t *current_buffer = NULL;
    size_t count = 0;

    // Toma inicialmente un buffer libre (bloqueante si no hay, aunque al inicio hay 2)
    xQueueReceive(buf_free_queue, &buffer_index, portMAX_DELAY);
    current_buffer = (buffer_index == 0) ? buf1 : buf2;
    count = 0;

    const TickType_t tick_interval = pdMS_TO_TICKS(2);  // ~2 ms entre lecturas (~475Hz)
    TickType_t next_wake = xTaskGetTickCount();

    configure_ad8233_sdn_pin();
    ESP_LOGI(TAG_ADC, "Activando el sensor AD8233...");
    gpio_set_level(SDN_AD8233, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Bucle principal de adquisición
    while (1) {
        // Leer una muestra del ADS1115
        int16_t sample;
        if (ads1115_read_conversion(&sample) == ESP_OK) {
            current_buffer[count++] = sample;
        } else {
            ESP_LOGW(TAG_ADC, "Error leyendo muestra ADC");
        }

        // Si completamos el buffer actual
        if (count >= SAMPLES_PER_BUFFER) {
            // Enviar índice de buffer lleno a la cola para procesamiento
            xQueueSend(buf_full_queue, &buffer_index, portMAX_DELAY);
            ESP_LOGD(TAG_ADC, "Buffer %d lleno, enviado a procesamiento", buffer_index);
            // Tomar el siguiente buffer libre para continuar (espera si ninguno libre)
            xQueueReceive(buf_free_queue, &buffer_index, portMAX_DELAY);
            current_buffer = (buffer_index == 0) ? buf1 : buf2;
            count = 0;
        }

        // Esperar hasta el próximo tick de muestreo (~2.1ms). 
        // Se usa vTaskDelayUntil para temporización periódica precisa.
        vTaskDelayUntil(&next_wake, tick_interval);
    }
}
