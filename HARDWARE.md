# Documentación del Hardware del Dispositivo ECG

Este documento detalla el diseño del hardware, el rediseño y los problemas encontrados.

## Prototipo Inicial

El primer prototipo del dispositivo se basó en los siguientes componentes principales:

* **Microcontrolador:** ESP32 (módulo estándar).
* **Sensor ECG:** Módulo AD8232.
    * Ganancia configurada típicamente en 100 (interna del chip) multiplicada por la ganancia de las etapas de instrumentación y filtro, resultando en una ganancia total elevada (ej. ~1000).
* **Comunicación/Programación:** Conversor UART a Serie CP210x externo para la conexión con el PC.
* **Alimentación:** Directa a través del puerto USB.

## Rediseño del Hardware (Versión 2 - En Desarrollo)

Se inició un rediseño del hardware con los siguientes objetivos principales:

1.  **Portabilidad y Gestión de Batería:**
    * Añadir los componentes necesarios para la alimentación mediante una batería recargable.
    * Componentes seleccionados para la gestión de energía:
        * **TP4056:** Módulo cargador de baterías.
        * **MP2161GJ:** Convertidor DC-DC step-down para regular el voltaje de la batería al voltaje operativo del ESP32 (3.3V).
        * **DW01A:** Circuito de protección para la batería (contra sobrecarga, sobredescarga y sobrecorriente).

2.  **Mejora de la Señal de ECG y Reducción de Ruido:**
    * **Sustitución del Sensor:** Cambiar el módulo AD8232 por el chip **AD8233**. El AD8233 ofrece más flexibilidad y es una revisión del  componente reciente.
    * **Reducción de Ganancia:** Ajustar la ganancia de la etapa de amplificación para evitar la saturación de la señal. Se planteó reducir la ganancia total de aproximadamente 1000 a **200**.

3.  **Actualización del Microcontrolador:**
    * Sustituir el ESP32 estándar por un **ESP32-S3**.
    * **Objetivo:** La principal motivación para este cambio es la capacidad del ESP32-S3 para ejecutar modelos de machine learning más complejos directamente en el dispositivo (TinyML), con la intención de **incorporar la red U-NET** para el filtrado de ruido en el propio microcontrolador en futuras iteraciones.

## Problemas Encontrados en el Rediseño

Durante las pruebas del hardware rediseñado, se encontró un problema crítico:

* **Ruido del Convertidor DC-DC:** El convertidor DC-DC **MP2161GJ**, encargado de suministrar los 3.3V al ESP32-S3 desde la batería, introducía una cantidad significativa de ruido eléctrico en el sistema.
* **Reinicios Constantes:** Este ruido afectaba la estabilidad del ESP32-S3, provocando que el pin de reset (ENABLE) se activara intermitentemente y el dispositivo se reiniciara constantemente. Esto impedía el funcionamiento normal del firmware.

## Próximos Pasos y Objetivos Futuros para el Hardware

Debido a los problemas de estabilidad causados por el MP2161GJ, el enfoque actual es:

1.  **Rediseño del Sistema de Alimentación:**
    * Investigar y seleccionar un regulador de voltaje alternativo que genere menos ruido.
    * Se considera el uso de un **LDO (Low-Dropout Regulator)** en lugar de un convertidor DC-DC conmutado para la alimentación del ESP32-S3 y los componentes analógicos sensibles. Aunque los LDOs son menos eficientes energéticamente que los convertidores conmutados, su salida es considerablemente más limpia y con menos ruido, lo cual es crucial para aplicaciones con señales analógicas sensibles como el ECG.


2.  **Validación del AD8233:** Una vez estabilizada la alimentación, proceder con la integración y prueba exhaustiva del AD8233 con la nueva configuración de ganancia.

3.  **Integración de la U-NET en ESP32-S3:** Cuando el hardware sea estable y la adquisición de datos sea fiable, retomar el objetivo de implementar la inferencia de la red U-NET directamente en el ESP32-S3.