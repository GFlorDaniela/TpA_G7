from flask import Flask, request, jsonify
from flask_cors import CORS
import json
import os

app = Flask(__name__)
CORS(app)

# Archivos para persistencia
USUARIOS_FILE = "usuarios.json"
PREGUNTAS_FILE = "preguntas.json"

def cargar_datos(archivo):
    """Carga datos desde archivo JSON"""
    if os.path.exists(archivo):
        with open(archivo, 'r', encoding='utf-8') as f:
            return json.load(f)
    return []

def guardar_datos(archivo, datos):
    """Guarda datos en archivo JSON"""
    with open(archivo, 'w', encoding='utf-8') as f:
        json.dump(datos, f, ensure_ascii=False, indent=2)

# Cargar datos al iniciar
usuarios = cargar_datos(USUARIOS_FILE)
preguntas = cargar_datos(PREGUNTAS_FILE)

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
    """Crea un nuevo usuario"""
    try:
        data = request.get_json()
        nombre = data.get('nombre', '').upper().strip()
        
        if not nombre:
            return jsonify({"error": "Nombre requerido"}), 400
        
        # Verificar si ya existe
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
        
        # Guardar en archivo
        guardar_datos(USUARIOS_FILE, usuarios)
        
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
        guardar_datos(PREGUNTAS_FILE, preguntas)
        
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
    """Marca el inicio de una partida"""
    try:
        # Por ahora solo registramos el evento
        print("🎮 Partida iniciada")
        return jsonify({"mensaje": "Partida iniciada"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/modo_partida', methods=['POST'])
def establecer_modo_partida():
    """Establece el modo de partida"""
    try:
        data = request.get_json()
        modo = data.get('tipo', '')
        print(f"🎮 Modo de partida establecido: {modo}")
        return jsonify({"mensaje": f"Modo {modo} establecido"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/accion/actualizar_puntaje', methods=['POST'])
def actualizar_puntaje():
    """Actualiza el puntaje de un usuario"""
    try:
        data = request.get_json()
        nombre = data.get('nombre', '').upper()
        puntaje = data.get('puntaje', 0)
        
        # Buscar usuario
        for usuario in usuarios:
            if usuario['nombre'] == nombre:
                usuario['partidas'] = usuario.get('partidas', 0) + 1
                if puntaje > usuario.get('puntos', 0):
                    usuario['puntos'] = puntaje
                
                guardar_datos(USUARIOS_FILE, usuarios)
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
    app.run(host='0.0.0.0', port=8000, debug=True)