import paho.mqtt.client as mqtt
import os
from pathlib import Path

# --- Configuración ---
MQTT_BROKER = "test.mosquitto.org"  # Cambia esto a la dirección de tu broker
MQTT_PORT = 1883
DATA_DIR = "./data"


# --- Funciones de MQTT ---

def on_connect(client, userdata, flags, rc):
    """Función que se ejecuta cuando el cliente se conecta al broker."""
    print(f"Conectado con código de resultado: {rc}")
    # Nos suscribimos al tópico
    client.subscribe("esp32/archivos/preguntas.json")
    client.subscribe("esp32/archivos/usuarios.json")
    print(f"Suscrito correctamente")

def on_message(client, userdata, msg):
    """Función que se ejecuta cuando se recibe un mensaje MQTT."""
    print(f"\n[Mensaje recibido] Tópico: {msg.topic}")
    
    # El payload es el contenido completo del archivo
    file_content = msg.payload.decode('utf8')
    partes = msg.topic.split("/")
    name = partes[2]
    # Llama a la función de sincronización de archivos
    sync_file_content(file_content, name)

def sync_file_content(content, name):
    """Crea la carpeta si no existe y sobrescribe el archivo."""
    try:
        # 1. Crear el directorio si no existe (método conciso de Python)
        os.makedirs(DATA_DIR, exist_ok=True) 
        full_path = Path(DATA_DIR) / name
        # 2. SOBRESCRIBIR el archivo (método síncrono conciso)
        # 'w' abre el archivo para escritura, sobrescribiendo el contenido.
        with open(full_path, 'w', encoding='utf8') as f:
            f.write(content)
            
        print(f"✅ Archivo '{name}' sincronizado con éxito.")
        print(f"   Contenido: {content.splitlines()[0]}...")
        
    except Exception as e:
        print(f"⚠️ Error al sincronizar el archivo: {e}")

# --- Ejecución principal ---

if __name__ == "__main__":
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1) # O VERSION2
    client.on_connect = on_connect
    client.on_message = on_message

    # Conectar al broker
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        # Bucle de procesamiento de red. Bloqueante, pero mantiene el cliente ejecutándose.
        client.loop_forever()
    except Exception as e:
        print(f"Error de conexión al broker MQTT: {e}")
