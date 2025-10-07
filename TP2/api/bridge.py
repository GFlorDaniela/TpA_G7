import paho.mqtt.client as mqtt
from telegram import Update
from telegram.ext import ApplicationBuilder, CommandHandler, ContextTypes
import requests

TOKEN = "8238234652:AAEVwkqELgLiu8f_RpWsZlKfxq9azuSubUI"
MQTT_SERVER = "test.mosquitto.org"
TOPIC = "wokwi/acciones"
API_URL = "http://tu_api.com/accion"

# MQTT setup
mqtt_client = mqtt.Client()
mqtt_client.connect(MQTT_SERVER, 1883, 60)

# === Comandos del BOT ===

async def ranking(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Consulta el ranking actual"""
    try:
        r = requests.get(f"{API_URL}/ranking")
        if r.status_code == 200:
            ranking = r.json().get("ranking", [])
            if ranking:
                mensaje = "🏆 *Ranking Actual:*\n"
                for i, u in enumerate(ranking, start=1):
                    mensaje += f"{i}. {u['nombre']} - {u['puntos']} pts ({u['partidas']} partidas)\n"
            else:
                mensaje = "⚠️ No hay usuarios registrados aún."
        else:
            mensaje = "Error al obtener el ranking."
    except Exception as e:
        mensaje = f"❌ Error al conectar con la API: {e}"

    await update.message.reply_text(mensaje, parse_mode="Markdown")

async def cargar_pregunta(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Carga una nueva pregunta"""
    try:
        texto = " ".join(context.args)
        if not texto:
            await update.message.reply_text("⚠️ Usa el formato: /cargar_pregunta pregunta;op1;op2;op3;correcta")
            return
        requests.post(f"{API_URL}/pregunta", json={"texto": texto})
        await update.message.reply_text("✅ Pregunta cargada correctamente.")
    except Exception as e:
        await update.message.reply_text(f"❌ Error: {e}")

async def ingresar_usuario(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Registra un nuevo usuario"""
    try:
        if len(context.args) == 0:
            await update.message.reply_text("⚠️ Usa: /ingresar_usuario NombreUsuario")
            return
        nombre = context.args[0].upper()
        requests.post(f"{API_URL}/usuario", json={"nombre": nombre})
        await update.message.reply_text(f"👤 Usuario '{nombre}' ingresado correctamente.")
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

# === NUEVO COMANDO ===
async def seleccionar_partida(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Permite seleccionar el tipo de partida: 1vs1, ranking o multijugador
    """
    opciones_validas = ["1vs1", "ranking", "multijugador"]

    if len(context.args) == 0:
        mensaje = (
            "🎮 *Selecciona el tipo de partida:*\n"
            "/seleccionar_partida 1vs1 — Jugar contra otro usuario\n"
            "/seleccionar_partida ranking — Jugar contra el jugador con mayor ranking\n"
            "/seleccionar_partida multijugador — Jugar con varios usuarios"
        )
        await update.message.reply_text(mensaje, parse_mode="Markdown")
        return

    tipo = context.args[0].lower()
    if tipo not in opciones_validas:
        await update.message.reply_text("⚠️ Tipo no válido. Usa: 1vs1, ranking o multijugador")
        return

    # Publica el tipo de partida al ESP32
    mqtt_client.publish(TOPIC, f"modo:{tipo}")

    # Notifica a la API (opcional)
    try:
        requests.post(f"{API_URL}/modo_partida", json={"tipo": tipo})
    except:
        pass  # Si falla la API, no interrumpe el flujo

    await update.message.reply_text(f"✅ Modo de partida seleccionado: *{tipo.upper()}*", parse_mode="Markdown")


# === Configuración del bot ===
app = ApplicationBuilder().token(TOKEN).build()

app.add_handler(CommandHandler("ranking", ranking))
app.add_handler(CommandHandler("cargar_pregunta", cargar_pregunta))
app.add_handler(CommandHandler("ingresar_usuario", ingresar_usuario))
app.add_handler(CommandHandler("iniciar_partida", iniciar_partida))
app.add_handler(CommandHandler("seleccionar_partida", seleccionar_partida))

print("🤖 Bot corriendo...")
app.run_polling()
