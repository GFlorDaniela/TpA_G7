import paho.mqtt.client as mqtt
import os
import json
from telegram import Update
from telegram.ext import ApplicationBuilder, CommandHandler, ContextTypes
import requests
import threading
import time

TOKEN = "8238234652:AAEVwkqELgLiu8f_RpWsZlKfxq9azuSubUI"
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
                # Suscribirse al tópico para escuchar pedidos desde el ESP
                client.subscribe(MQTT_TOPIC)
                print(f"📡 Suscripto a {MQTT_TOPIC}")
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
            print(f"📰 Mensaje MQTT recibido en {msg.topic}: {payload}")
            if payload == 'pedir_usuarios':
                print("📣 Pedido de usuarios recibido -> republicando usuarios retenidos")
                publicar_usuarios_existentes()
        except Exception as e:
            print(f"⚠️ Error manejando mensaje MQTT: {e}")

    client.on_message = on_message

    try:
        client.connect(MQTT_SERVER, 1883, 60)
        client.loop_start()
        return client
    except Exception as e:
        print(f"❌ Error MQTT: {e}")
        return None

mqtt_client = setup_mqtt()

# === Comandos del BOT ===

async def ranking(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Consulta el ranking actual"""
    print(f"📊 Comando /ranking recibido")
    try:
        r = requests.get(f"{API_URL}/ranking")
        if r.status_code == 200:
            data = r.json()
            ranking = data.get("ranking", [])
            if ranking:
                mensaje = "🏆 *Ranking Actual:*\n"
                for i, u in enumerate(ranking, start=1):
                    mensaje += f"{i}. {u['nombre']} - {u.get('puntos', 0)} pts ({u.get('partidas', 0)} partidas)\n"
            else:
                mensaje = "⚠️ No hay usuarios registrados aún."
        else:
            mensaje = "❌ Error al obtener el ranking."
    except Exception as e:
        mensaje = f"❌ Error al conectar con la API: {e}"

    await update.message.reply_text(mensaje, parse_mode="Markdown")

async def cargar_pregunta(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Carga una nueva pregunta"""
    print(f"❓ Comando /cargar_pregunta recibido")
    try:
        texto = " ".join(context.args)
        if not texto:
            await update.message.reply_text("⚠️ Usa: /cargar_pregunta pregunta;op1;op2;op3;correcta")
            return
        
        r = requests.post(f"{API_URL}/pregunta", json={"texto": texto})
        
        if r.status_code == 201:
            await update.message.reply_text("✅ Pregunta cargada correctamente.")
        else:
            error = r.json().get('error', 'Error desconocido')
            await update.message.reply_text(f"❌ Error: {error}")
            
    except Exception as e:
        await update.message.reply_text(f"❌ Error: {e}")

async def ingresar_usuario(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Registra un nuevo usuario"""
    print(f"👤 Comando /ingresar_usuario recibido")
    try:
        if len(context.args) == 0:
            await update.message.reply_text("⚠️ Usa: /ingresar_usuario NombreUsuario")
            return
        
        nombre = context.args[0].upper()
        r = requests.post(f"{API_URL}/usuario", json={"nombre": nombre})
        
        if r.status_code in [200, 201]:
            data = r.json()
            # Publicar por MQTT para notificar al ESP32
            try:
                mqtt_msg = f"usuario:{nombre}"
                print(f"📤 Publicando MQTT usuario: {mqtt_msg} (retain)")
                mqtt_client.publish(MQTT_TOPIC, mqtt_msg, qos=1, retain=True)
                # Also publish a non-retained creation event so ESP can auto-select the newly created user
                created_msg = f"usuario_created:{nombre}"
                print(f"📤 Publicando evento creación (no-retain): {created_msg}")
                mqtt_client.publish(MQTT_TOPIC, created_msg, qos=1, retain=False)
            except Exception as e:
                print(f"⚠️ Error publicando usuario por MQTT: {e}")

            await update.message.reply_text(f"👤 {data.get('mensaje', 'Usuario registrado')}")
        else:
            error = r.json().get('error', 'Error desconocido')
            await update.message.reply_text(f"❌ Error: {error}")
            
    except Exception as e:
        await update.message.reply_text(f"❌ Error: {e}")

async def iniciar_partida(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Inicia una nueva partida"""
    print(f"🎮 Comando /iniciar_partida recibido")
    try:
        print("📡 Enviando a API...")
        requests.post(f"{API_URL}/iniciar_partida")
        
        print(f"📤 Publicando a MQTT: 'iniciar_partida' en topic: {MQTT_TOPIC}")
        result = mqtt_client.publish(MQTT_TOPIC, "iniciar_partida")
        print(f"📤 Resultado de publicación: {result}")
        
        await update.message.reply_text("🎮 Partida iniciada en el ESP32.")
        print("✅ Respuesta enviada a Telegram")
        
    except Exception as e:
        print(f"❌ Error en iniciar_partida: {e}")
        await update.message.reply_text(f"❌ Error: {e}")

async def seleccionar_partida(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Selecciona el tipo de partida"""
    print(f"🎯 Comando /seleccionar_partida recibido")
    opciones_validas = ["1vs1", "ranking", "multijugador"]

    if len(context.args) == 0:
        mensaje = (
            "🎮 *Selecciona el tipo de partida:*\n"
            "/seleccionar_partida 1vs1 — Jugar contra otro usuario\n"
            "/seleccionar_partida ranking — Jugar contra el mejor\n"
            "/seleccionar_partida multijugador — Varios usuarios"
        )
        await update.message.reply_text(mensaje, parse_mode="Markdown")
        return

    tipo = context.args[0].lower()
    print(f"🎯 Modo seleccionado: {tipo}")
    
    if tipo not in opciones_validas:
        await update.message.reply_text("⚠️ Tipo no válido. Usa: 1vs1, ranking o multijugador")
        return

    # Enviar a API y ESP32
    try:
        print("📡 Enviando a API...")
        requests.post(f"{API_URL}/modo_partida", json={"tipo": tipo})
    except Exception as e:
        print(f"⚠️ Error en API (continuando): {e}")
    
    mensaje_mqtt = f"modo:{tipo}"
    print(f"📤 Publicando a MQTT: '{mensaje_mqtt}' en topic: {MQTT_TOPIC}")
    result = mqtt_client.publish(MQTT_TOPIC, mensaje_mqtt)
    print(f"📤 Resultado de publicación: {result}")
    
    await update.message.reply_text(f"✅ Modo seleccionado: *{tipo.upper()}*", parse_mode="Markdown")
    print("✅ Respuesta enviada a Telegram")

async def estado(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Estado del sistema"""
    print(f"📊 Comando /estado recibido")
    try:
        r = requests.get(f"{API_URL}/estado")
        if r.status_code == 200:
            data = r.json()
            mensaje = "📊 *Estado del Sistema:*\n"
            mensaje += f"👥 Usuarios: {data.get('usuarios', 0)}\n"
            mensaje += f"❓ Preguntas: {data.get('preguntas', 0)}\n"
            mensaje += f"🟢 Estado: {data.get('estado', 'desconocido')}"
        else:
            mensaje = "❌ Error al conectar con la API"
    except Exception as e:
        mensaje = f"❌ Error: {e}"

    await update.message.reply_text(mensaje, parse_mode="Markdown")

# === Configuración del bot ===
print("🤖 Inicializando bot de Telegram...")
app = ApplicationBuilder().token(TOKEN).build()

app.add_handler(CommandHandler("ranking", ranking))
app.add_handler(CommandHandler("cargar_pregunta", cargar_pregunta))
app.add_handler(CommandHandler("ingresar_usuario", ingresar_usuario))
app.add_handler(CommandHandler("iniciar_partida", iniciar_partida))
app.add_handler(CommandHandler("seleccionar_partida", seleccionar_partida))
app.add_handler(CommandHandler("estado", estado))
app.add_handler(CommandHandler("start", estado))

print("🤖 Bot corriendo con API...")
print("💡 Envía comandos desde Telegram y verifica los mensajes aquí")

# Publicar usuarios existentes desde la API para sincronizar con ESP32
def publicar_usuarios_existentes():
    try:
        print("🔎 Solicitando usuarios existentes a la API...")
        r = requests.get(f"{API_URL}/ranking")
        if r.status_code == 200:
            data = r.json()
            ranking = data.get('ranking', [])
            for u in ranking:
                nombre = normalize_name(u.get('nombre', ''))
                if nombre:
                    mqtt_msg = f"usuario:{nombre}"
                    print(f"📤 Publicando usuario existente por MQTT: {mqtt_msg} (retain)")
                    mqtt_client.publish(MQTT_TOPIC, mqtt_msg, qos=1, retain=True)
        else:
            print(f"⚠️ No se pudo obtener ranking: {r.status_code}")
    except Exception as e:
        print(f"⚠️ Error solicitando usuarios: {e}")
        # Fallback: intentar leer archivo local api/usuarios.json
        try:
            base = os.path.dirname(__file__)
            usuarios_file = os.path.join(base, 'usuarios.json')
            print(f"🔁 Intentando fallback leyendo: {usuarios_file}")
            with open(usuarios_file, 'r', encoding='utf-8') as fh:
                data = json.load(fh)
                usuarios = data.get('usuarios') if isinstance(data, dict) else data
                if usuarios:
                    for u in usuarios:
                        nombre = normalize_name(u.get('nombre', '') if isinstance(u, dict) else str(u))
                        if nombre:
                            mqtt_msg = f"usuario:{nombre}"
                            print(f"📤 (fallback) Publicando usuario por MQTT: {mqtt_msg} (retain)")
                            mqtt_client.publish(MQTT_TOPIC, mqtt_msg, qos=1, retain=True)
                else:
                    print("⚠️ Fallback: no se encontraron usuarios en el archivo")
        except Exception as e2:
            print(f"❌ Fallback falló: {e2}")


def poll_and_publish_new_users(interval=10):
    """Background thread: poll ranking and publish new usuarios to MQTT"""
    known = set()
    # seed with current ranking
    try:
        r = requests.get(f"{API_URL}/ranking")
        if r.status_code == 200:
            for u in r.json().get('ranking', []):
                nombre = normalize_name(u.get('nombre',''))
                if nombre:
                    known.add(nombre)
    except Exception:
        pass

    while True:
        try:
            r = requests.get(f"{API_URL}/ranking")
            if r.status_code == 200:
                for u in r.json().get('ranking', []):
                    nombre = normalize_name(u.get('nombre',''))
                    if nombre and nombre not in known:
                        known.add(nombre)
                        mqtt_msg = f"usuario:{nombre}"
                        print(f"📤 (poll) Publicando nuevo usuario por MQTT: {mqtt_msg} (retain)")
                        mqtt_client.publish(MQTT_TOPIC, mqtt_msg, qos=1, retain=True)
        except Exception as e:
            print(f"⚠️ Poll error: {e}")
        time.sleep(interval)


publicar_usuarios_existentes()
# Start background polling thread to publish new users automatically
poll_thread = threading.Thread(target=poll_and_publish_new_users, args=(10,), daemon=True)
poll_thread.start()
app.run_polling()