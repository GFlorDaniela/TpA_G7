from flask import Flask, request, jsonify
from flask_cors import CORS
import json
import os
import threading
import paho.mqtt.client as mqtt
import time

app = Flask(__name__)
CORS(app)

# Archivos para persistencia
USUARIOS_FILE = "usuarios.json"
PREGUNTAS_FILE = "preguntas.json"

# MQTT settings to notify ESP/bridge about user changes
MQTT_SERVER = "test.mosquitto.org"
MQTT_TOPIC = "wokwi/acciones"
mqtt_client = None

def setup_mqtt():
    global mqtt_client
    try:
        mqtt_client = mqtt.Client()
        mqtt_client.connect(MQTT_SERVER, 1883, 60)
        mqtt_client.loop_start()
        print(f"🔌 MQTT connected to {MQTT_SERVER}")
        return True
    except Exception as e:
        mqtt_client = None
        print(f"⚠️ MQTT setup failed: {e}")
        return False

def normalize_name(nombre: str) -> str:
    if not nombre:
        return ""
    s = nombre.strip().upper()

    if s.startswith("<") and s.endswith(">"):
        s = s[1:-1].strip()

    s = s.replace('<', '').replace('>', '')
    return s

def cargar_datos(archivo):
    """Carga datos desde archivo JSON"""
    if os.path.exists(archivo):
        with open(archivo, 'r', encoding='utf-8') as f:
            return json.load(f)
    return []

def guardar_datos(archivo, datos):
    """Guarda datos en archivo JSON"""
    try:
        with open(archivo, 'w', encoding='utf-8') as f:
            json.dump(datos, f, ensure_ascii=False, indent=2)
    except Exception as e:
        print(f"⚠️ Error guardando {archivo}: {e}")

# Cargar datos al iniciar
usuarios = cargar_datos(USUARIOS_FILE)
preguntas = cargar_datos(PREGUNTAS_FILE)

# Normalizar y eliminar duplicados en usuarios
def normalize_and_dedupe_users():
    global usuarios
    seen = {}
    new_list = []
    for u in usuarios:
        nombre = normalize_name(u.get('nombre', ''))
        if not nombre:
            continue
        # merge if exists: keep max puntos and sum partidas
        if nombre in seen:
            existing = seen[nombre]
            existing['puntos'] = max(existing.get('puntos', 0), u.get('puntos', 0))
            existing['partidas'] = existing.get('partidas', 0) + u.get('partidas', 0)
        else:
            new_u = {
                'nombre': nombre,
                'puntos': u.get('puntos', 0),
                'partidas': u.get('partidas', 0)
            }
            seen[nombre] = new_u
            new_list.append(new_u)
    usuarios = new_list
    guardar_datos(USUARIOS_FILE, usuarios)

normalize_and_dedupe_users()
setup_mqtt()

# === RUTAS DE LA API ===

@app.route('/accion/ranking', methods=['GET'])
def obtener_ranking():
    """Obtiene el ranking de usuarios"""
    try:
        # Ordenar por puntos (descendente) y partidas (descendente)
        ranking_ordenado = sorted(usuarios, 
                                key=lambda x: (-x.get('puntos', 0), -x.get('partidas', 0)))
        
        return jsonify({
            "ranking": ranking_ordenado[:10]  # Top 10
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/usuario', methods=['POST'])
def crear_usuario():
    """Crea un nuevo usuario - OPTIMIZADO"""
    try:
        data = request.get_json()
        nombre = normalize_name(data.get('nombre', ''))
        
        if not nombre:
            return jsonify({"error": "Nombre requerido"}), 400
        
        # ⚡ BÚSQUEDA RÁPIDA
        for usuario in usuarios:
            if usuario['nombre'] == nombre:
                return jsonify({"mensaje": "Usuario ya existe"}), 200
        
        # Crear nuevo usuario
        nuevo_usuario = {
            "nombre": nombre,
            "puntos": 0,
            "partidas": 0
        }
        usuarios.append(nuevo_usuario)
        
        # ⚡ GUARDADO EN SEGUNDO PLANO
        threading.Thread(target=guardar_datos, args=(USUARIOS_FILE, usuarios)).start()

        # ⚡ MQTT SIN BLOQUEAR
        if mqtt_client:
            def publicar_mqtt():
                try:
                    mqtt_client.publish(MQTT_TOPIC, f"usuario:{nombre}", qos=1, retain=True)
                    mqtt_client.publish(MQTT_TOPIC, f"usuario_created:{nombre}", qos=0, retain=False)
                except Exception as e:
                    print(f"⚠️ Error MQTT async: {e}")
            
            threading.Thread(target=publicar_mqtt).start()

        return jsonify({"mensaje": f"Usuario {nombre} creado"}), 201
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/pregunta', methods=['POST'])
def crear_pregunta():
    """Crea una nueva pregunta"""
    try:
        data = request.get_json()
        texto = data.get('texto', '').strip()
        
        if not texto:
            return jsonify({"error": "Texto requerido"}), 400
        
        # Parsear el formato: pregunta;op1;op2;op3;correcta
        partes = texto.split(';')
        if len(partes) != 5:
            return jsonify({"error": "Formato: pregunta;op1;op2;op3;correcta"}), 400
        
        try:
            correcta = int(partes[4])
            if correcta not in [0, 1, 2]:
                return jsonify({"error": "Correcta debe ser 0, 1 o 2"}), 400
        except ValueError:
            return jsonify({"error": "Correcta debe ser número"}), 400
        
        nueva_pregunta = {
            "id": len(preguntas) + 1,
            "texto": partes[0],
            "opciones": [
                partes[1],  # opción 0
                partes[2],  # opción 1  
                partes[3]   # opción 2
            ],
            "correcta": correcta,
            "puntaje": 10  # Puntaje fijo por ahora
        }
        
        preguntas.append(nueva_pregunta)
        # ⚡ GUARDADO EN SEGUNDO PLANO
        threading.Thread(target=guardar_datos, args=(PREGUNTAS_FILE, preguntas)).start()
        
        return jsonify({"mensaje": "Pregunta creada", "id": nueva_pregunta["id"]}), 201
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/preguntas', methods=['GET'])
def obtener_preguntas():
    """Obtiene todas las preguntas (para el ESP32)"""
    try:
        return jsonify({
            "preguntas": preguntas,
            "total": len(preguntas)
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/iniciar_partida', methods=['POST'])
def iniciar_partida():
    """Marca el inicio de una partida - OPTIMIZADO"""
    try:
        print("🎮 Partida iniciada")
        # ⚡ RESPUESTA INMEDIATA
        return jsonify({"mensaje": "Partida iniciada"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/modo_partida', methods=['POST'])
def establecer_modo_partida():
    """Establece el modo de partida - OPTIMIZADO"""
    try:
        data = request.get_json()
        modo = data.get('tipo', '')
        print(f"🎮 Modo de partida establecido: {modo}")
        # ⚡ RESPUESTA INMEDIATA
        return jsonify({"mensaje": f"Modo {modo} establecido"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/actualizar_puntaje', methods=['POST'])
def actualizar_puntaje():
    """Actualiza el puntaje de un usuario - OPTIMIZADO"""
    try:
        data = request.get_json()
        nombre = normalize_name(data.get('nombre', ''))
        puntaje = data.get('puntaje', 0)
        
        # Buscar usuario
        for usuario in usuarios:
            if usuario['nombre'] == nombre:
                usuario['partidas'] = usuario.get('partidas', 0) + 1
                if puntaje > usuario.get('puntos', 0):
                    usuario['puntos'] = puntaje
                
                # ⚡ GUARDADO EN SEGUNDO PLANO
                threading.Thread(target=guardar_datos, args=(USUARIOS_FILE, usuarios)).start()
                
                return jsonify({
                    "mensaje": f"Puntaje actualizado para {nombre}",
                    "puntos": usuario['puntos'],
                    "partidas": usuario['partidas']
                }), 200
        
        return jsonify({"error": "Usuario no encontrado"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/estado', methods=['GET'])
def estado():
    """Estado general del sistema"""
    return jsonify({
        "usuarios": len(usuarios),
        "preguntas": len(preguntas),
        "estado": "activo"
    }), 200

if __name__ == '__main__':
    print("🚀 Iniciando API del Quiz...")
    print(f"👥 Usuarios cargados: {len(usuarios)}")
    print(f"❓ Preguntas cargadas: {len(preguntas)}")
    app.run(host='0.0.0.0', port=8000, debug=False)  # debug=False para mejor rendimiento