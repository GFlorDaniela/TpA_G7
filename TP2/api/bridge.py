import paho.mqtt.client as mqtt
import os
import json
from telegram import Update
from telegram.ext import ApplicationBuilder, CommandHandler, ContextTypes
import requests
import threading
import time

TOKEN = "8376405384:AAH_30BV0A7zlZotdfKpx3KucxvUtSanau8"
MQTT_SERVER = "test.mosquitto.org"
MQTT_TOPIC = "wokwi/acciones"
API_URL = "http://localhost:8000/accion"

def normalize_name(nombre: str) -> str:
    if not nombre:
        return ''
    s = nombre.strip().upper()
    if s.startswith('<') and s.endswith('>'):
        s = s[1:-1].strip()
    s = s.replace('<','').replace('>','')
    return s

# MQTT setup
def setup_mqtt():
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print("✅ Conectado a MQTT Broker!")
            try:
                client.subscribe(MQTT_TOPIC)
                print(f"📡 Suscrito a {MQTT_TOPIC}")
            except Exception as e:
                print(f"⚠️ Error suscribiendo al tópico: {e}")
        else:
            print(f"❌ Error conectando MQTT: {rc}")
    
    def on_publish(client, userdata, mid):
        print(f"📤 Mensaje MQTT publicado (ID: {mid})")
    
    def on_disconnect(client, userdata, rc):
        print("⚠️ Desconectado de MQTT")

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_publish = on_publish
    client.on_disconnect = on_disconnect
    
    def on_message(client, userdata, msg):
        try:
            payload = msg.payload.decode('utf-8')
            print(f"📰 MQTT recibido: {payload}")
            if payload == 'pedir_usuarios':
                print("📣 Pedido de usuarios recibido")
                publicar_usuarios_existentes()
        except Exception as e:
            print(f"⚠️ Error MQTT: {e}")

    client.on_message = on_message

    try:
        client.connect(MQTT_SERVER, 1883, 60)
        client.loop_start()
        return client
    except Exception as e:
        print(f"❌ Error MQTT: {e}")
        return None

mqtt_client = setup_mqtt()

# === COMANDOS DEL BOT CORREGIDOS ===

async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Muestra los comandos disponibles y el estado del sistema"""
    print(f"🚀 Comando /start recibido")
    
    # Mensaje simplificado SIN Markdown problemático
    mensaje_comandos = (
        "🎮 QUIZ ESP32 - COMANDOS DISPONIBLES:\n\n"
        "📊 CONSULTAS:\n"
        "• /start - Muestra esta ayuda y el estado\n"
        "• /estado - Estado del sistema\n"
        "• /ranking - Ranking de jugadores\n\n"
        
        "👤 GESTIÓN DE USUARIOS:\n"
        "• /ingresar_usuario NOMBRE - Crear nuevo usuario\n"
        "  Ejemplo: /ingresar_usuario SOFI\n\n"
        
        "🎯 CONFIGURACIÓN DE JUEGO:\n"
        "• /seleccionar_partida MODO - Elegir modo de juego\n"
        "  Modos: 1vs1, ranking, multijugador\n"
        "  Ejemplo: /seleccionar_partida 1vs1\n"
        "• /iniciar_partida - Comenzar partida\n\n"
        
        "❓ GESTIÓN DE PREGUNTAS:\n"
        "• /cargar_pregunta FORMATO - Agregar nueva pregunta\n"
        "  Formato: pregunta;op1;op2;op3;correcta\n"
        "  Ejemplo: /cargar_pregunta Capital de Italia;Roma;Paris;Londres;0"
    )
    
    await update.message.reply_text(mensaje_comandos)  # SIN parse_mode="Markdown"
    
    # Luego mostrar el estado actual del sistema
    await estado(update, context)

async def ranking(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Consulta el ranking actual"""
    print(f"📊 Comando /ranking recibido")
    try:
        r = requests.get(f"{API_URL}/ranking", timeout=5)
        if r.status_code == 200:
            data = r.json()
            ranking = data.get("ranking", [])
            if ranking:
                mensaje = "🏆 Ranking Actual:\n"
                for i, u in enumerate(ranking, start=1):
                    mensaje += f"{i}. {u['nombre']} - {u.get('puntos', 0)} pts ({u.get('partidas', 0)} partidas)\n"
            else:
                mensaje = "⚠️ No hay usuarios registrados aún."
        else:
            mensaje = "❌ Error al obtener el ranking."
    except Exception as e:
        mensaje = f"❌ Error al conectar con la API: {e}"

    await update.message.reply_text(mensaje)  # SIN Markdown

async def cargar_pregunta(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Carga una nueva pregunta"""
    print(f"❓ Comando /cargar_pregunta recibido")
    try:
        texto = " ".join(context.args)
        if not texto:
            await update.message.reply_text(
                "⚠️ Uso correcto:\n"
                "/cargar_pregunta pregunta;op1;op2;op3;correcta\n\n"
                "Ejemplo:\n"
                "/cargar_pregunta Capital de Italia;Roma;Paris;Londres;0"
            )
            return
        
        r = requests.post(f"{API_URL}/pregunta", json={"texto": texto}, timeout=5)
        
        if r.status_code == 201:
            await update.message.reply_text("✅ Pregunta cargada correctamente.")
        else:
            error = r.json().get('error', 'Error desconocido')
            await update.message.reply_text(f"❌ Error: {error}")
            
    except Exception as e:
        await update.message.reply_text(f"❌ Error: {e}")

async def ingresar_usuario(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Registra o selecciona un usuario existente"""
    print(f"👤 Comando /ingresar_usuario recibido")
    try:
        if len(context.args) == 0:
            await update.message.reply_text(
                "⚠️ Uso correcto:\n"
                "/ingresar_usuario NOMBRE\n\n"
                "Ejemplo:\n"
                "/ingresar_usuario SOFI"
            )
            return
        
        nombre = context.args[0].upper()
        
        # ⚡ SIEMPRE enviar usuario_created (tanto para nuevos como existentes)
        try:
            mqtt_msg = f"usuario_created:{nombre}"
            print(f"📤 Publicando MQTT: {mqtt_msg}")
            mqtt_client.publish(MQTT_TOPIC, mqtt_msg, qos=0)
        except Exception as e:
            print(f"⚠️ Error MQTT: {e}")

        # ⚡ RESPUESTA INMEDIATA
        await update.message.reply_text(f"👤 Usuario {nombre} seleccionado/creado")
        
        # Procesar API después sin bloquear (solo para nuevos usuarios)
        def procesar_api():
            try:
                r = requests.post(f"{API_URL}/usuario", json={"nombre": nombre}, timeout=3)
                if r.status_code == 201:  # Solo si es nuevo usuario
                    mqtt_client.publish(MQTT_TOPIC, f"usuario:{nombre}", qos=1, retain=True)
                    print(f"✅ Usuario NUEVO {nombre} confirmado por API")
                elif r.status_code == 200:  # Usuario ya existe
                    print(f"ℹ️ Usuario EXISTENTE {nombre} - solo selección")
                else:
                    print(f"⚠️ Error API usuario: {r.status_code}")
            except Exception as e:
                print(f"⚠️ API lenta: {e}")
        
        threading.Thread(target=procesar_api).start()
            
    except Exception as e:
        await update.message.reply_text(f"❌ Error: {e}")

async def iniciar_partida(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Inicia una nueva partida"""
    print(f"🎮 Comando /iniciar_partida recibido")
    
    try:
        print(f"📤 Publicando a MQTT: 'iniciar_partida'")
        mqtt_client.publish(MQTT_TOPIC, "iniciar_partida", qos=0)
        await update.message.reply_text("🎮 Iniciando partida en el ESP32...")
    except Exception as e:
        await update.message.reply_text(f"❌ Error MQTT: {e}")
        return
    
    # Procesar API después sin bloquear
    def procesar_api():
        try:
            requests.post(f"{API_URL}/iniciar_partida", timeout=2)
        except Exception as e:
            print(f"⚠️ API lenta: {e}")

    threading.Thread(target=procesar_api).start()
    print("✅ Comando iniciar_partida procesado")

async def seleccionar_partida(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Selecciona el tipo de partida"""
    print(f"🎯 Comando /seleccionar_partida recibido")
    opciones_validas = ["1vs1", "ranking", "multijugador"]

    if len(context.args) == 0:
        mensaje = (
            "🎮 Selecciona el tipo de partida:\n\n"
            "• /seleccionar_partida 1vs1 — Jugar contra otro usuario\n"
            "• /seleccionar_partida ranking — Jugar contra el mejor\n"
            "• /seleccionar_partida multijugador — Varios usuarios\n\n"
            "Ejemplo: /seleccionar_partida 1vs1"
        )
        await update.message.reply_text(mensaje)
        return

    tipo = context.args[0].lower()
    print(f"🎯 Modo seleccionado: {tipo}")
    
    if tipo not in opciones_validas:
        await update.message.reply_text("⚠️ Tipo no válido. Usa: 1vs1, ranking o multijugador")
        return

    # Enviar inmediatamente a MQTT
    mensaje_mqtt = f"modo:{tipo}"
    print(f"📤 Publicando a MQTT: '{mensaje_mqtt}'")
    mqtt_client.publish(MQTT_TOPIC, mensaje_mqtt, qos=0)
    
    # Respuesta inmediata a Telegram
    await update.message.reply_text(f"✅ Modo seleccionado: {tipo.upper()}")
    
    # Procesar API después sin bloquear
    def procesar_api():
        try:
            requests.post(f"{API_URL}/modo_partida", json={"tipo": tipo}, timeout=2)
        except Exception as e:
            print(f"⚠️ API lenta: {e}")

    threading.Thread(target=procesar_api).start()
    print("✅ Comando procesado")

async def estado(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Estado del sistema"""
    print(f"📊 Comando /estado recibido")
    try:
        r = requests.get(f"{API_URL}/estado", timeout=5)
        if r.status_code == 200:
            data = r.json()
            mensaje = (
                "📊 Estado del Sistema:\n"
                f"• 👥 Usuarios: {data.get('usuarios', 0)}\n"
                f"• ❓ Preguntas: {data.get('preguntas', 0)}\n"
                f"• 🟢 Estado: {data.get('estado', 'desconocido')}\n\n"
                "Usa /start para ver todos los comandos disponibles"
            )
        else:
            mensaje = "❌ Error al conectar con la API"
    except Exception as e:
        mensaje = f"❌ Error: {e}"

    await update.message.reply_text(mensaje)

# === Configuración del bot ===
print("🤖 Inicializando bot de Telegram...")
app = ApplicationBuilder().token(TOKEN).build()

# Configurar los handlers
app.add_handler(CommandHandler("start", start))
app.add_handler(CommandHandler("ranking", ranking))
app.add_handler(CommandHandler("cargar_pregunta", cargar_pregunta))
app.add_handler(CommandHandler("ingresar_usuario", ingresar_usuario))
app.add_handler(CommandHandler("iniciar_partida", iniciar_partida))
app.add_handler(CommandHandler("seleccionar_partida", seleccionar_partida))
app.add_handler(CommandHandler("estado", estado))

print("🤖 Bot corriendo con API...")
print("💡 Envía /start en Telegram para ver todos los comandos disponibles")

# Publicar usuarios existentes desde la API para sincronizar con ESP32
def publicar_usuarios_existentes():
    try:
        print("🔎 Sincronizando usuarios desde API...")
        r = requests.get(f"{API_URL}/ranking", timeout=10)
        if r.status_code == 200:
            data = r.json()
            ranking = data.get('ranking', [])
            for u in ranking:
                nombre = normalize_name(u.get('nombre', ''))
                if nombre:
                    mqtt_msg = f"usuario:{nombre}"
                    print(f"📤 Publicando usuario: {mqtt_msg}")
                    mqtt_client.publish(MQTT_TOPIC, mqtt_msg, qos=1, retain=True)
        else:
            print(f"⚠️ No se pudo obtener ranking: {r.status_code}")
    except Exception as e:
        print(f"⚠️ Error sincronizando usuarios: {e}")

publicar_usuarios_existentes()
app.run_polling()