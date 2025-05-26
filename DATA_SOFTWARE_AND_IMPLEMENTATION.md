# Adquisición de Datos, Desarrollo de U-NET e Implementación de Software

Este documento describe el proceso de adquisición y análisis de datos para el entrenamiento de la red neuronal U-NET, así como la arquitectura del software desarrollado para el ESP32 y el servidor TCP en el PC.

## Adquisición y Preparación de Datos para la Red U-NET

La robustez de la red neuronal U-NET para la eliminación de ruido en señales de ECG depende en gran medida de la calidad de los datos de entrenamiento.

1.  **Base de Datos de Referencia:**
    * Se utilizaron datos de la base de datos **MIT-BIH Arrhythmia Database**. Esta es la colección más usada de grabaciones de ECG, que incluye una variedad de ritmos cardíacos y artefactos.

2.  **Generación de Ruido Artificial:**
    * Para entrenar la U-NET se añadió ruido de forma artificial a las señales limpias de la base de datos MIT.
    * El tipo y la magnitud del ruido añadido se basaron en las características del **ruido observado en el primer prototipo del dispositivo ECG**. Esto incluye:
        * Ruido de línea de alimentación (50/60 Hz).
        * Ruido de movimiento muscular (EMG).
        * Deriva de la línea base.

    * El objetivo era crear pares de datos: (señal ruidosa aritificial, señal limpia original) para el entrenamiento supervisado de la U-NET.

3.  **Segmentación de Datos:**
    * Las señales de ECG (tanto las limpias como las ruidosas) se segmentaron en ventanas de tamaño fijo, correspondientes al tamaño de entrada esperado por la arquitectura U-NET de 720 muestras.

## Arquitectura del Software en ESP32 (FreeRTOS)

El firmware del ESP32 se ha desarrollado en C utilizando el sistema operativo en tiempo real FreeRTOS para gestionar las diferentes tareas de forma eficiente.

### `main`: Inicialización y Creación de Tareas

La función principal `app_main()` se encarga de:

1.  **Inicializar la Conexión Wi-Fi:** Configura el ESP32 para conectarse a una red Wi-Fi especificada.
2.  **Iniciar el Servidor TCP:** Una vez conectado a la red Wi-Fi, el ESP32 levanta un servidor TCP en un puerto específico, esperando la conexión de un cliente (el PC).
3.  **Creación de Buffers:** Se definen dos buffers de datos globales (ej. `buf1`, `buf2`). Cada buffer tiene una capacidad de 950 muestras (`int16_t`).
4.  **Creación de Colas de FreeRTOS:**
    * `buf_full_queue`: Una cola para enviar los índices de los buffers que se han llenado de datos crudos.
    * `buf_free_queue`: Una cola para enviar los índices de los buffers que han sido procesados y están listos para ser reutilizados.
    Inicialmente, los índices de ambos buffers se añaden a `buf_free_queue`.
5.  **Creación y Lanzamiento de Tareas:** Se crean y asignan dos tareas principales, cada una anclada a un núcleo diferente del ESP32 para un procesamiento paralelo:
    * **Tarea de Adquisición de ADC (Core 1):** `adc_capture_task()`
    * **Tarea de Procesamiento y Envío (Core 2):** `data_processing_task()`

### `adc_capture_task` (Ejecutándose en Core 1)

Esta tarea es responsable de la adquisición continua de datos del ADC:

1.  **Esperar Buffer Libre:** Intenta obtener un índice de buffer de `buf_free_queue`. Si no hay buffers libres, la tarea se bloquea hasta que uno esté disponible.
2.  **Lectura de Muestras:** Lee muestras del ADC (conectado al AD8232/AD8233) a una frecuencia de 475 SPS.
3.  **Llenado de Buffer:** Almacena las muestras leídas (`int16_t`) en el buffer actualmente asignado.
4.  **Buffer Lleno:** Una vez que el buffer actual (950 muestras) está lleno:
    * Envía el índice de este buffer lleno a `buf_full_queue` para que la tarea del Core 2 lo procese.
    * Vuelve al paso 1 para obtener otro buffer libre y continuar la adquisición.
5.  **Temporización:** Se utiliza `vTaskDelayUntil` para asegurar que las muestras se lean a intervalos regulares, manteniendo la frecuencia de muestreo de 475 SPS.

### `data_processing_task` (Ejecutándose en Core 2)

Esta tarea se encarga de procesar los datos y enviarlos al cliente TCP:

1.  **Esperar Buffer Lleno:** Intenta obtener un índice de buffer de `buf_full_queue`. Si no hay buffers llenos, la tarea se bloquea.
2.  **Remuestreo (Interpolación Lineal):**
    * Recibe el buffer de 950 muestras (que representan 2 segundos de datos a 475 SPS).
    * Aplica una **interpolación lineal** para reducir la frecuencia de muestreo de 475 SPS a 360 SPS. Esto resulta en un buffer de 720 muestras (2 segundos de datos).
3.  **Envío de Datos por TCP:**
    * Envía el buffer de 720 muestras (ya interpoladas) al cliente TCP conectado.
4.  **Liberar Buffer:** Una vez que los datos han sido enviados, envía el índice del buffer (ahora libre) de vuelta a `buf_free_queue`.
5.  **Futura Implementación de U-NET:**
    * Está previsto que en esta sección de la tarea del Core 2 se integre la inferencia de la red U-NET si se ejecuta directamente en el ESP32-S3. Actualmente, la inferencia se realiza en el PC.

**Gestión de Buffers:**
El uso de dos buffers y dos colas implementa un mecanismo de "ping-pong buffering". Mientras un buffer se está llenando con nuevas muestras en el Core 1, el Core 2 puede estar procesando y enviando el contenido del otro buffer. Esto asegura que no se pierdan datos durante el procesamiento y la transmisión, optimizando el flujo continuo de datos.

## Aplicación en el PC (Servidor TCP y Visualización)

El script en Python que se ejecuta en el PC actúa como:

1.  **Servidor TCP:**
    * Escucha en un puerto específico (9000) esperando la conexión del ESP32.
    * Una vez conectado, recibe los paquetes de datos (720 muestras de `int16_t`).

2.  **Decodificación y Conversión:**
    * Decodifica los bytes recibidos en un array de valores `int16_t`.
    * **Convierte estas lecturas ADC a milivoltios (mV)** basándose en la resolución del ADC (16 bits) y el voltaje de referencia configurado (+- 4.096 V).

3.  **Inferencia con U-NET:**
    * Los datos se preparan para la entrada de la red U-NET.
    * Se realiza la inferencia utilizando el modelo U-NET pre-entrenado (cargado desde un archivo `.keras`).
    * La salida de la U-NET es la señal de ECG filtrada/denoised.

4.  **Visualización en Tiempo Real:**
    * Utiliza `matplotlib` con `FuncAnimation` para actualizar dos gráficos en tiempo real:
        * **Gráfico 1:** Muestra la señal de ECG original (convertida a mV/V).
        * **Gráfico 2:** Muestra la señal de ECG después de pasar por la inferencia de la U-NET.
    * Ambos gráficos se actualizan a medida que llegan nuevos paquetes de datos del ESP32.