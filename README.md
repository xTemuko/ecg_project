# Proyecto ECG con ESP32 y Red Neuronal U-NET para Filtrado de Ruido

Este proyecto se centra en el desarrollo de un  electrocardiograma (ECG) basado en un microcontrolador ESP32. El sistema captura señales cardíacas, las procesa y las transmite a través de un servidor TCP a un ordenador. En el ordenador, una red neuronal con estructura U-NET filtra el ruido de la señal, y se visualizan en tiempo real tanto la señal cruda como la señal filtrada.

## Características Principales

* **Captura de ECG:** Utiliza un biosensor (inicialmente AD8232, con planes de migración a AD8233) para adquirir la señal de ECG. La señal es leída por un ADC y enviada al ESP32.
* **Procesamiento en ESP32:** El ESP32 se encarga de leer los datos del ADC, realizar un preprocesamiento inicial (remuestreo mediante interpolación lineal) y gestionar la comunicación.
* **Comunicación TCP:** Los datos procesados se envían a un servidor TCP implementado en un PC.
* **Filtrado con Red Neuronal U-NET:** Una red U-NET que se ejecuta en el PC recibe los datos y realiza el filtrado de ruido.
* **Visualización en Tiempo Real:** Una aplicación en el PC grafica simultáneamente la señal de ECG cruda (convertida a milivoltios) y la señal tras pasar por la inferencia de la U-NET.
* **Conectividad:** El ESP32 se conecta al ordenador a través de un conversor UART a Serie (CP210x) para programación y depuración, y mediante Wi-Fi para la transmisión de datos TCP.

## Flujo de Trabajo del Proyecto

1.  **Adquisición de Señal:** El sensor AD8232/AD8233 capta la actividad eléctrica del corazón.
2.  **Lectura y Digitalización:** El ESP32 lee la señal digital del ADC1115 a través de un una interfaz I2C y la prepara para preprocesar.
3.  **Procesamiento en el Embebido:** El ESP32 aplica interpolación lineal para ajustar la frecuencia de muestreo.
4.  **Transmisión TCP:** El ESP32 envía los datos procesados al servidor TCP en el PC.
5.  **Inferencia U-NET:** El servidor Python en el PC recibe los datos, y los introduce en la red U-NET para eliminar el ruido.
6.  **Visualización:** Se muestran dos gráficas en tiempo real: la señal cruda en mV y la señal filtrada por la U-NET.

## Estado del Proyecto

Actualmente, el sistema es capaz de capturar, transmitir y visualizar los datos. Se está trabajando en el rediseño del hardware para mejorar la portabilidad y reducir el ruido, así como en la posible integración de la inferencia de la U-NET directamente en un ESP32-S3.