#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "ecg_config.h"


// Wi-Fi 
#define WIFI_SSID       "MOVISTAR_DB78"
#define WIFI_PASS       "iqbenVcwzhEsEofhjFWC"
#define MAX_WIFI_RETRY  5
#define SERVER_IP       "192.168.1.77"     // IP del servidor TCP destino
#define SERVER_PORT     9000               // Puerto del servidor

// Generamos las colas de Freertos
int16_t buf1[SAMPLES_PER_BUFFER];
int16_t buf2[SAMPLES_PER_BUFFER];
QueueHandle_t buf_free_queue;   // cola de índices de buffer libres
QueueHandle_t buf_full_queue;   // cola de índices de buffer llenos para procesar

// Socket TCP global
int tcp_socket = -1;

// Tareas para cada core (dfubudas en .h)
void core1_adc_task(void *arg);
void core2_process_task(void *arg);

// Event group y bits para WiFi (para sincronización de conexión)
#include "freertos/event_groups.h"
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT      = BIT1;
static int s_retry_num = 0;
static const char *TAG = "main";  // Tag para logs

// Manejador de eventos WiFi
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_WIFI_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Reintentando conectar al AP...");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi conectado, dirección IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Función de inicialización WiFi en modo estación (STA)
static void wifi_init_sta() {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());                    // Inicializa la TCP/IP stack:contentReference[oaicite:3]{index=3}
    ESP_ERROR_CHECK(esp_event_loop_create_default());     
    esp_netif_create_default_wifi_sta();                  
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                
    // Registra handlers para eventos WiFi e IP
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Configuración de la conex WIFI
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // nivel de seguridad mínimo
            .pmf_cfg = { .capable = true, .required = false }
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wifi iniciada, conectando a SSID:%s", WIFI_SSID);
    // Espera a que esté conectado o que falle
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado al AP %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "No se pudo conectar al AP %s", WIFI_SSID);
    }
    // (En un código de producción, manejar reintentos o fallos adicionales)
}

// Inicialización del bus I2C para el ADS1115
static void i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
}

// Conexión TCP al servidor
static int tcp_connect_to_server() {
    ESP_LOGI(TAG, "Conectando por TCP a %s:%d", SERVER_IP, SERVER_PORT);
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);
    int sock =  socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Error creando socket: %d", sock);
        return -1;
    }
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Error conectando socket: errno %d", err);
        close(sock);
        return -1;
    }
    ESP_LOGI(TAG, "Conexión TCP establecida");
    return sock;
}

void app_main(void) {
    // Inicializar NVS (requerido por WiFi para almacenar credenciales/calibraciones)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // En caso de que la partición NVS esté llena o se requiera actualizar, borrar y reiniciar
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Inicializa I2C y WiFi
    i2c_master_init();
    wifi_init_sta();
    // Una vez conectado WiFi, establece conexión TCP
    tcp_socket = tcp_connect_to_server();
    if (tcp_socket < 0) {
        ESP_LOGE(TAG, "No se pudo abrir socket TCP, se aborta.");
        vTaskSuspend(NULL);
    }

    // Crear cola para buffers libres (2 buffers) y cola para buffers llenos
    buf_free_queue = xQueueCreate(2, sizeof(int));
    buf_full_queue = xQueueCreate(2, sizeof(int));
    // Inicializar la cola de buffers libres con ambos índices (0 y 1)
    int idx0 = 0, idx1 = 1;
    xQueueSend(buf_free_queue, &idx0, 0);
    xQueueSend(buf_free_queue, &idx1, 0);

    // Crear tareas en cada núcleo
    // Tarea de adquisición ADC en Núcleo 1 (prioridad alta)
    xTaskCreatePinnedToCore(core1_adc_task, "ADC_Task", 4096, NULL, configMAX_PRIORITIES-1, NULL, 1);
    // Tarea de procesamiento/envío en Núcleo 2 (Core 0) con prioridad ligeramente menor
    xTaskCreatePinnedToCore(core2_process_task, "Proc_Task", 4096, NULL, configMAX_PRIORITIES-2, NULL, 0);

    // El núcleo principal ya configuró todo, puede entrar en suspensión.
    // (Opcionalmente podríamos borrar esta tarea main si no se usa más)
    vTaskDelete(NULL);
}
