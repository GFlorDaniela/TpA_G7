# config.py
# Configuración MQTT - Modificar según tu entorno

MQTT_CONFIG = {
    "broker_host": "test.mosquitto.org",      # IP o hostname del broker MQTT
    "broker_port": 1883,             # Puerto MQTT (1883 por defecto)
    "client_id": "file_receiver_python",
    "username": None,                # Usuario MQTT (None si no requiere)
    "password": None,                # Password MQTT (None si no requiere)
    "keepalive": 60,                 # Keepalive en segundos
    "topic_base": "esp32/archivos/"  # Tema base para suscripción
}

# Configuración de archivos
FILE_CONFIG = {
    "data_directory": "data",
    "default_extension": ".json",
    "max_file_size": 10 * 1024 * 1024  # 10MB máximo
}