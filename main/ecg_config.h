#ifndef ECG_CONFIG_H    /* Evita inclusiones múltiples */
#define ECG_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Constantes de tamaño de búfer */
#define SAMPLES_PER_BUFFER 950    /**< Número de muestras por búfer (tamaño del búfer doble) */
#define DOWNSAMPLED_SAMPLES 720   /**< Número de muestras tras reducción de muestreo (downsampling) */

#define I2C_MASTER_NUM      I2C_NUM_0        // Puerto I2C
#define I2C_MASTER_SDA_IO   8              // Pin SDA
#define I2C_MASTER_SCL_IO   9              // Pin SCL
#define I2C_MASTER_FREQ_HZ  400000          // Frecuencia I2C 400kHz
#define ADS1115_ADDR        0x48            // Dirección I2C del ADS1115 (ADDR a GND)
#define ADS1115_REG_CONFIG  0x01            // Registro de configuración
#define ADS1115_REG_CONV    0x00   

extern int tcp_socket;

/* Declaración de los búferes dobles para datos ECG */
extern int16_t buf1[SAMPLES_PER_BUFFER];
extern int16_t buf2[SAMPLES_PER_BUFFER];

/* Declaración de las colas FreeRTOS para gestión de búferes */
extern QueueHandle_t buf_free_queue;  /**< Cola de búferes libres */
extern QueueHandle_t buf_full_queue;  /**< Cola de búferes llenos */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ECG_CONFIG_H */
