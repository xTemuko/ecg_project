#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "ecg_config.h"

extern int tcp_socket;


static const char *TAG_PROC = "Proc_Task";

// Función de downsampling simple: reduce 950 muestras a 720 
// Interpolación lineal teniendo en cuenta la variación temporal entre muestras
static void downsample_950_to_720(int16_t *in_buf, int16_t *out_buf) {
    const float ratio = 950.0f / DOWNSAMPLED_SAMPLES;

    for (int i = 0; i < DOWNSAMPLED_SAMPLES; ++i) {
        float new_input_position = (float)i * ratio;
        int input_index_prev = (int)new_input_position;
        int input_index_next = input_index_prev + 1;
        
        float fraction = new_input_position - (float)input_index_prev;
        
        out_buf[i] = (int16_t)( (float)in_buf[input_index_prev] * (1.0f - fraction) + 
                                (float)in_buf[input_index_next] * fraction );
    }
}

void core2_process_task(void *arg) {
    int buf_index;
    static int16_t proc_buffer[DOWNSAMPLED_SAMPLES];  // buffer para datos procesados (720 muestras)

    while (1) {
        // Esperar hasta que haya un buffer lleno para procesar (bloqueante)
        if (xQueueReceive(buf_full_queue, &buf_index, portMAX_DELAY) == pdTRUE) {
            // Seleccionar el buffer correspondiente
            int16_t *raw_data = (buf_index == 0) ? buf1 : buf2;
            ESP_LOGD(TAG_PROC, "Recibido buffer %d para procesar", buf_index);

            // Downsampling de 950 -> 720 muestras
            downsample_950_to_720(raw_data, proc_buffer);

            // **Punto para integración de TinyML U-Net**:

            // Envío del paquete TCP con los datos procesados (720 muestras de 16 bits)
            int bytes_to_send = DOWNSAMPLED_SAMPLES * sizeof(int16_t);
            int sent = send(tcp_socket, (char*)proc_buffer, bytes_to_send, 0);
            if (sent < 0) {
                ESP_LOGE(TAG_PROC, "Error al enviar datos TCP: errno %d", errno);
            } else {
                ESP_LOGI(TAG_PROC, "Enviadas %d muestras (buffer %d) por TCP", DOWNSAMPLED_SAMPLES, buf_index);
            }

            // Liberar el buffer crudo ya procesado, devolviendo su índice a la cola de libres
            xQueueSend(buf_free_queue, &buf_index, portMAX_DELAY);
        }
    }
}
