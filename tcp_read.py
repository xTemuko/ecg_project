import socket
import numpy as np
import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import struct
import time
from keras import models # Para cargar el modelo Keras

# --- Configuración del Servidor TCP ---
IP = '0.0.0.0'    
PORT = 9000        
DOWNSAMPLED_SAMPLES = 720 
BYTES_PER_SAMPLE = 2      
EXPECTED_PACKET_SIZE = DOWNSAMPLED_SAMPLES * BYTES_PER_SAMPLE 

# --- Constantes para Conversión ADC a Voltios ---
ADC_MAX_POSITIVE_VALUE = 32767.0 
ADC_VOLTAGE_FULL_SCALE_POSITIVE = 2.048 # EJEMPLO: +/- 2.048V. ¡¡¡AJUSTA ESTO!!!
ADC_TO_VOLTS_FACTOR = ADC_VOLTAGE_FULL_SCALE_POSITIVE / ADC_MAX_POSITIVE_VALUE

# --- Carga del Modelo U-Net ---
MODEL_PATH = "ecg_denoising_unet_v_final_.keras" 
model_unet = None
try:
    model_unet = models.load_model(MODEL_PATH)
    print(f"Modelo U-Net cargado exitosamente desde: {MODEL_PATH}")
except Exception as e:
    print(f"ERROR al cargar el modelo U-Net desde {MODEL_PATH}: {e}")
    print("La graficación continuará con datos crudos solamente.")

# --- Configuración del Gráfico ---
fig, axs = plt.subplots(2, 1, figsize=(12, 10), sharex=True) 
ax_raw = axs[0]
ax_denoised = axs[1]
x_data = np.arange(DOWNSAMPLED_SAMPLES) 

# --- Buffers y Flags ---
latest_ecg_data_volts = np.zeros(DOWNSAMPLED_SAMPLES) # Almacena datos en Voltios
latest_denoised_data_zscore = np.zeros(DOWNSAMPLED_SAMPLES) # Almacena datos filtrados (ahora Z-score)
byte_buffer = b''
animation_running = True

# --- Variables de Conexión ---
sock = None
conn = None

def setup_plot_axes():
    """Limpia y configura los ejes de AMBOS gráficos."""
    
    ax_raw.clear() 
    y_limit_raw = ADC_VOLTAGE_FULL_SCALE_POSITIVE * 1.1 
    ax_raw.set_ylim(-y_limit_raw, y_limit_raw) 
    ax_raw.set_title("ECG Crudo en Tiempo Real (ESP32)")
    ax_raw.set_ylabel("Voltaje (V)") 
    ax_raw.grid(True) 

    ax_denoised.clear()
    # Límites para el gráfico de datos filtrados (AHORA Z-SCORED)
    # Un rango típico para Z-scores es de -3 a 3 o -4 a 4 para capturar la mayoría de las variaciones.
    ax_denoised.set_ylim(-4, 4) # ¡AJUSTA ESTO SI ES NECESARIO según la salida del modelo!
    ax_denoised.set_title("ECG Filtrado en Tiempo Real (U-Net, salida Z-score)")
    x_tick_interval = DOWNSAMPLED_SAMPLES // 10
    ax_denoised.set_xlabel(f"Muestras (intervalo de {x_tick_interval} muestras)")
    ax_denoised.set_ylabel("Amplitud Normalizada (Z-score)") # Etiqueta actualizada
    ax_denoised.grid(True)
    
    fig.tight_layout() 

def update_plot(frame):
    global byte_buffer, latest_ecg_data_volts, latest_denoised_data_zscore, animation_running, conn, ani, model_unet

    if not animation_running or conn is None:
        return 

    new_data_received = False
    try:
        data_chunk = conn.recv(4096) 
        if not data_chunk:
            print("Conexión cerrada por el ESP32. Deteniendo la animación.")
            animation_running = False
            if ani and hasattr(ani, 'event_source') and ani.event_source: 
                ani.event_source.stop()
            return
        byte_buffer += data_chunk
    except socket.timeout:
        pass 
    except socket.error as e:
        print(f"Error de socket: {e}. Deteniendo la animación.")
        animation_running = False
        if ani and hasattr(ani, 'event_source') and ani.event_source:
            ani.event_source.stop()
        return

    if len(byte_buffer) >= EXPECTED_PACKET_SIZE: 
        packet = byte_buffer[:EXPECTED_PACKET_SIZE]
        byte_buffer = byte_buffer[EXPECTED_PACKET_SIZE:]
        new_data_received = True

        try:
            unpacked_data = struct.unpack(f'<{DOWNSAMPLED_SAMPLES}h', packet)
            raw_adc_values = np.array(unpacked_data)
            latest_ecg_data_volts = raw_adc_values * ADC_TO_VOLTS_FACTOR
        except struct.error as e:
            print(f"Error al desempaquetar datos: {e}. Tamaño del paquete: {len(packet)} bytes.")
            latest_ecg_data_volts = np.zeros(DOWNSAMPLED_SAMPLES) 
            new_data_received = False 

    if new_data_received and model_unet is not None:
        try:
            # --- Preparación de datos para el modelo U-Net ---
            # `latest_ecg_data_volts` ya contiene los datos en escala de Voltios (float)

            # 1. Aplicar Z-score normalization
            mean_val = np.mean(latest_ecg_data_volts)
            std_val = np.std(latest_ecg_data_volts)

            input_data_model_normalized = None
            if std_val > 1e-6: # Evitar división por cero o por un valor muy pequeño
                input_data_model_normalized = (latest_ecg_data_volts - mean_val) / std_val
            else:
                # Si la desviación estándar es casi cero (señal plana), 
                # la normalización Z-score no está bien definida o resultaría en NaN/Inf.
                # En este caso, pasamos datos centrados en cero (o todos ceros).
                input_data_model_normalized = latest_ecg_data_volts - mean_val # O np.zeros_like(latest_ecg_data_volts)
            
            # 2. Reshape para el modelo: (1, 720, 1)
            input_data_model_reshaped = input_data_model_normalized.reshape(1, DOWNSAMPLED_SAMPLES, 1)
            
            # --- Predicción ---
            denoised_output = model_unet.predict(input_data_model_reshaped)
            
            # --- Post-procesamiento de la salida ---
            # La salida del modelo (si la entrada fue Z-scored) probablemente también esté en una escala Z-score.
            squeezed_denoised_output = denoised_output.squeeze()
            latest_denoised_data_zscore = squeezed_denoised_output

            if latest_denoised_data_zscore.ndim > 1:
                latest_denoised_data_zscore = latest_denoised_data_zscore.flatten()
            if latest_denoised_data_zscore.shape[0] != DOWNSAMPLED_SAMPLES:
                print(f"Advertencia: Forma inesperada de datos filtrados Z-score: {latest_denoised_data_zscore.shape}")
                latest_denoised_data_zscore = np.zeros(DOWNSAMPLED_SAMPLES) 

        except Exception as e:
            print(f"Error durante la predicción/procesamiento del modelo U-Net: {e}")
            latest_denoised_data_zscore = np.zeros(DOWNSAMPLED_SAMPLES) 
    elif new_data_received and model_unet is None:
        latest_denoised_data_zscore = np.zeros(DOWNSAMPLED_SAMPLES) 

    setup_plot_axes() 
    
    ax_raw.plot(x_data, latest_ecg_data_volts, color='dodgerblue', label="ECG Crudo (V)")
    ax_denoised.plot(x_data, latest_denoised_data_zscore, color='orangered', label="ECG Filtrado (U-Net, Z-score)")
    
# --- Bucle Principal y Animación ---
# (El resto del bloque try/except/finally permanece igual)
ani = None 
try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((IP, PORT))
    sock.listen(1)
    print(f"Esperando conexión del ESP32 en {IP}:{PORT}...")
    conn, addr = sock.accept()
    print(f"Conectado desde {addr}")
    conn.settimeout(0.05) 

    setup_plot_axes()
    ax_raw.plot(x_data, latest_ecg_data_volts, color='dodgerblue') 
    ax_denoised.plot(x_data, latest_denoised_data_zscore, color='orangered')

    ani = FuncAnimation(fig, update_plot, interval=100, blit=False, cache_frame_data=False)

    plt.show() 

except KeyboardInterrupt:
    print("Animación interrumpida por el usuario.")
except Exception as e:
    print(f"Ocurrió un error inesperado: {e}")
finally:
    print("Cerrando la aplicación...")
    animation_running = False 
    if ani and hasattr(ani, 'event_source') and ani.event_source is not None:
        try:
            ani.event_source.stop()
        except Exception as e_stop:
            print(f"Error al detener event_source de la animación: {e_stop}")
    
    time.sleep(0.2) 
    if conn:
        try:
            conn.close()
            print("Conexión TCP cerrada.")
        except Exception as e_conn:
            print(f"Error al cerrar la conexión del cliente: {e_conn}")
    if sock:
        try:
            sock.close()
            print("Socket del servidor cerrado.")
        except Exception as e_sock:
            print(f"Error al cerrar el socket del servidor: {e_sock}")
    print("Aplicación finalizada.")