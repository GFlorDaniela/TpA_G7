import paho.mqtt.client as mqtt
from telegram import Update
from telegram.ext import ApplicationBuilder, CommandHandler, ContextTypes
import requests
import threading

TOKEN = "8499799877:AAFrKrXnz7mkbJlnY6zNcUCdD-oG9UM_uIY"
MQTT_SERVER = "test.mosquitto.org"
TOPIC = "wokwi/acciones"
API_URL = "http://localhost:8000/accion"

# MQTT setup
def setup_mqtt():
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print("✅ Conectado a MQTT Broker!")
        else:
            print(f"❌ Error conectando MQTT: {rc}")
    def on_disconnect(client, userdata, rc):
        print("⚠️ Desconectado de MQTT")


    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect

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
    try:
        if len(context.args) == 0:
            await update.message.reply_text("⚠️ Usa: /ingresar_usuario NombreUsuario")
            return
        
        nombre = context.args[0].upper()
        r = requests.post(f"{API_URL}/usuario", json={"nombre": nombre})
        
        if r.status_code in [200, 201]:
            data = r.json()
            await update.message.reply_text(f"👤 {data.get('mensaje', 'Usuario registrado')}")
        else:
            error = r.json().get('error', 'Error desconocido')
            await update.message.reply_text(f"❌ Error: {error}")
            
    except Exception as e:
        await update.message.reply_text(f"❌ Error: {e}")

async def iniciar_partida(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Inicia una nueva partida"""
    try:
        requests.post(f"{API_URL}/iniciar_partida")
        mqtt_client.publish(TOPIC, "iniciar_partida")
        await update.message.reply_text("🎮 Partida iniciada en el ESP32.")
    except Exception as e:
        await update.message.reply_text(f"❌ Error: {e}")

async def seleccionar_partida(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Selecciona el tipo de partida"""
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
    if tipo not in opciones_validas:
        await update.message.reply_text("⚠️ Tipo no válido. Usa: 1vs1, ranking o multijugador")
        return

    # Enviar a API y ESP32
    try:
        requests.post(f"{API_URL}/modo_partida", json={"tipo": tipo})
    except:
        pass  # Si falla la API, continuamos
    
    mqtt_client.publish(TOPIC, f"modo:{tipo}")
    await update.message.reply_text(f"✅ Modo seleccionado: *{tipo.upper()}*", parse_mode="Markdown")

async def estado(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Estado del sistema"""
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
app = ApplicationBuilder().token(TOKEN).build()

app.add_handler(CommandHandler("ranking", ranking))
app.add_handler(CommandHandler("cargar_pregunta", cargar_pregunta))
app.add_handler(CommandHandler("ingresar_usuario", ingresar_usuario))
app.add_handler(CommandHandler("iniciar_partida", iniciar_partida))
app.add_handler(CommandHandler("seleccionar_partida", seleccionar_partida))
app.add_handler(CommandHandler("estado", estado))
app.add_handler(CommandHandler("start", estado))

print("🤖 Bot corriendo con API...")
app.run_polling()