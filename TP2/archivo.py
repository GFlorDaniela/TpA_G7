# mqtt_file_receiver_advanced.py
import paho.mqtt.client as mqtt
import os
import json
import time
from datetime import datetime
import logging

class AdvancedMQTTFileReceiver:
    def __init__(self, config):
        self.config = config
        self.data_dir = config["FILE_CONFIG"]["data_directory"]
        self.reconnect_delay = 5  # segundos entre reconexiones
        
        self._create_data_directory()
        self._setup_mqtt_client()
        
    def _create_data_directory(self):
        """Crear directorio data si no existe"""
        if not os.path.exists(self.data_dir):
            os.makedirs(self.data_dir)
            logging.info(f"Directorio '{self.data_dir}' creado")
    
    def _setup_mqtt_client(self):
        """Configurar cliente MQTT"""
        mqtt_config = self.config["MQTT_CONFIG"]
        
        self.client = mqtt.Client(
            client_id=mqtt_config["client_id"],
            clean_session=True
        )
        
        if mqtt_config["username"] and mqtt_config["password"]:
            self.client.username_pw_set(
                mqtt_config["username"], 
                mqtt_config["password"]
            )
        
        # Callbacks
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        self.client.on_subscribe = self._on_subscribe
        
    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            logging.info("✅ Conectado a MQTT broker")
            
            # Suscribirse al tema
            topic = self.config["MQTT_CONFIG"]["topic_base"] + "#"
            client.subscribe(topic, qos=1)
        else:
            logging.error(f"❌ Error de conexión: {rc}")
            
    def _on_subscribe(self, client, userdata, mid, granted_qos):
        logging.info(f"✅ Suscrito a temas. QoS: {granted_qos}")
        
    def _on_message(self, client, userdata, msg):
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8')
            
            logging.info(f"📨 Mensaje recibido - Tema: {topic}")
            
            # Procesar y guardar archivo
            filename = self._process_filename(topic)
            self._save_file_with_metadata(filename, payload, topic)
            
        except Exception as e:
            logging.error(f"❌ Error procesando mensaje: {str(e)}")
    
    def _process_filename(self, topic):
        """Procesar nombre de archivo desde el tema"""
        base_topic = self.config["MQTT_CONFIG"]["topic_base"]
        
        # Remover el tema base
        if topic.startswith(base_topic):
            filename_part = topic[len(base_topic):]
        else:
            filename_part = topic.replace('/', '_')
        
        # Limpiar nombre de archivo
        filename = "".join(c for c in filename_part if c.isalnum() or c in '._-')
        
        if not filename:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"archivo_{timestamp}"
        
        # Agregar extensión si no tiene
        if '.' not in filename:
            filename += self.config["FILE_CONFIG"]["default_extension"]
            
        return filename
    
    def _save_file_with_metadata(self, filename, content, topic):
        """Guardar archivo con metadatos"""
        try:
            filepath = os.path.join(self.data_dir, filename)
            
            # Verificar si existe y crear nombre único
            filepath = self._get_unique_filename(filepath)
            
            # Crear metadatos
            metadata = {
                "topic": topic,
                "received_at": datetime.now().isoformat(),
                "size_chars": len(content),
                "filename": os.path.basename(filepath)
            }
            
            # Guardar contenido principal
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
            
            # Guardar metadatos en archivo separado
            meta_filename = filepath + ".meta"
            with open(meta_filename, 'w', encoding='utf-8') as f:
                json.dump(metadata, f, indent=2)
            
            logging.info(f"💾 Archivo guardado: {filepath}")
            logging.info(f"   Tamaño: {len(content)} caracteres")
            logging.info(f"   Metadatos: {meta_filename}")
            
        except Exception as e:
            logging.error(f"❌ Error guardando archivo: {str(e)}")
    
    def _get_unique_filename(self, filepath):
        """Obtener nombre de archivo único"""
        if not os.path.exists(filepath):
            return filepath
            
        base, ext = os.path.splitext(filepath)
        counter = 1
        
        while True:
            new_filepath = f"{base}_{counter}{ext}"
            if not os.path.exists(new_filepath):
                return new_filepath
            counter += 1
    
    def _on_disconnect(self, client, userdata, rc):
        if rc != 0:
            logging.warning("🔌 Desconexión inesperada. Intentando reconectar...")
            time.sleep(self.reconnect_delay)
            self._reconnect()
        else:
            logging.info("🔌 Desconectado del broker")
    
    def _reconnect(self):
        """Reconectar al broker"""
        try:
            self.client.reconnect()
        except Exception as e:
            logging.error(f"❌ Error en reconexión: {str(e)}")
            time.sleep(self.reconnect_delay)
            self._reconnect()
    
    def connect(self):
        """Conectar al broker"""
        mqtt_config = self.config["MQTT_CONFIG"]
        try:
            self.client.connect(
                mqtt_config["broker_host"],
                mqtt_config["broker_port"],
                mqtt_config["keepalive"]
            )
        except Exception as e:
            logging.error(f"❌ Error conectando: {str(e)}")
            raise
    
    def start(self):
        """Iniciar el cliente"""
        try:
            logging.info("🚀 Iniciando cliente MQTT...")
            self.client.loop_forever()
        except KeyboardInterrupt:
            logging.info("🛑 Deteniendo cliente...")
            self.stop()
    
    def stop(self):
        """Detener el cliente"""
        self.client.disconnect()
        logging.info("👋 Cliente MQTT detenido")

# Uso
if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s'
    )
    
    from config import MQTT_CONFIG, FILE_CONFIG
    
    config = {
        "MQTT_CONFIG": MQTT_CONFIG,
        "FILE_CONFIG": FILE_CONFIG
    }
    
    receiver = AdvancedMQTTFileReceiver(config)
    
    try:
        receiver.connect()
        receiver.start()
    except Exception as e:
        logging.error(f"Error: {e}")