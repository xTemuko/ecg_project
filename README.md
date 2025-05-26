# Proyecto ECG con ESP32 y Red Neuronal U-NET para Filtrado de Ruido

Este proyecto se centra en el desarrollo de un  electrocardiograma (ECG) basado en un microcontrolador ESP32. El sistema captura la señal cardíacas, la procesa y la transmite a través de un servidor TCP al ordenador. En el ordenador, una red neuronal con estructura U-NET filtra el ruido, y se visualizan en tiempo real tanto la señal cruda como filtrada.

## Características Principales

* **Captura de ECG:** Utiliza un biosensor (inicialmente AD8232, con planes de migración a AD8233). La señal es leída por el ADC y enviada al ESP32.
* **Procesamiento en ESP32:** El ESP32 se encarga de leer los datos del ADC, realizar un preprocesamiento inicial (remuestreo mediante interpolación lineal) y gestionar la comunicación.
* **Comunicación TCP:** Los datos procesados se envían a un servidor TCP implementado en el ordenador.
* **Filtrado con Red Neuronal U-NET:** Una red U-NET que se ejecuta recibe los datos y realiza el filtrado de ruido.
* **Visualización en Tiempo Real:** Una aplicación en matplotlib, grafica simultáneamente la señal original y la señal tras pasar por la inferencia de la U-NET.
* **Conectividad:** El ESP32 se conecta al ordenador a través de un conversor UART a Serie (CP210x) para programación y depuración, y mediante Wi-Fi para la transmisión de datos TCP.



## Estado del Proyecto

Actualmente, el sistema es capaz de capturar, transmitir y visualizar los datos. Se está trabajando en el rediseño del hardware para mejorar la portabilidad y reducir el ruido, así como en la posible integración de la inferencia de la U-NET directamente en un ESP32-S3.