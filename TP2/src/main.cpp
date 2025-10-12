#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <HTTPClient.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Pines
#define LED_PIN 23
#define ENCODER_CLK 18
#define ENCODER_DT 5
#define ENCODER_SW 19

// WiFi y Telegram
const char *ssid = "Wokwi-GUEST";
const char *password = "";
const char *mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;
const char *API_HOST = "10.0.2.2"; // Cambiar si tu API corre en otra IP (ej. 192.168.x.x)

// Telegram
const String BOT_TOKEN = "8238234652:AAEVwkqELgLiu8f_RpWsZlKfxq9azuSubUI";
const String CHAT_ID = "2044158296";

// Separate clients: one plain client for MQTT, one secure client for Telegram (HTTPS)
WiFiClient mqttWiFiClient;
WiFiClientSecure secureClient;
PubSubClient mqttClient(mqttWiFiClient);
UniversalTelegramBot bot(BOT_TOKEN, secureClient);

// ==================== ESTRUCTURAS DE DATOS ====================
struct Pregunta
{
  String texto;
  String opciones[3];
  int respuestaCorrecta;
  int puntaje;
};

struct Usuario
{
  String nombre;
  int puntuacionMaxima;
  int partidasJugadas;
};

// SIMULACIÓN DE ARCHIVOS - Contenido de preguntas.txt
const char *ARCHIVO_PREGUNTAS[] = {
    "¿Qué lenguaje usa Arduino?;C++;Python;Java;0",
    "Capital de Francia;Roma;Madrid;París;2",
    "Color bandera Argentina;Azul;Blanco;Celeste;2",
    "¿2+2?;3;4;5;1",
    "Animal Australia;Canguro;Koala;Emú;0"};

// SIMULACIÓN DE ARCHIVOS - Contenido de puntajes.txt
const int ARCHIVO_PUNTAJES[] = {10, 15, 10, 5, 10};

// ==================== VARIABLES GLOBALES ====================
Pregunta preguntas[10];
Usuario usuarios[20];
int totalPreguntas = 5;
int totalUsuarios = 0;
int preguntaActual = 0;
int opcionSeleccionada = 0;
int puntuacionTotal = 0;
int usuarioActual = -1;
int oponenteActual = -1;
bool quizCompletado = false;
bool quizIniciado = false;
String modoPartida = "";

// Variables encoder/button (polling)
unsigned long lastButtonPress = 0;
// Ajuste de debounce más conservador para Wokwi
const unsigned long BUTTON_DEBOUNCE_MS = 50; // ms
unsigned long lastEncoderStep = 0;
const unsigned long ENCODER_STEP_MS = 50; // ms entre pasos para evitar rebotes rápidos
int lastEncoderState = 0;
int lastCLK = HIGH; // para encoder por flanco
int lastDT = HIGH;
int lastButtonState = HIGH;
bool encoderEnabled = true;
unsigned long encoderDisableUntil = 0;

// Flag para forzar actualización de pantalla cuando un evento externo lo modifica
volatile bool needsActualizarPantalla = false;

// (Se eliminan ISRs; usar polling en loop para Wokwi)

// Variables Telegram
unsigned long lastTimeBotRan = 0;
bool telegramConnected = false;

// ==================== NUEVOS ESTADOS ====================
enum EstadoSistema
{
  ESTADO_INICIO,
  ESTADO_SELECCION_MODO,
  ESTADO_SELECCION_USUARIO,
  ESTADO_ESPERA_INICIO,
  ESTADO_SELECCION_OPONENTE,
  ESTADO_JUGANDO,
  ESTADO_FINAL
};

EstadoSistema estadoActual = ESTADO_INICIO;
bool modoSeleccionadoPorTelegram = false;

// ==================== PROTOTIPOS DE FUNCIONES ====================
void mostrarPantallaConexiones();
void mostrarPantallaSeleccionModo();
void mostrarPantallaSeleccionUsuario();
void mostrarPantallaEsperaInicio();
void mostrarPantallaSeleccionOponente();
void mostrarPregunta();
void simularCargaArchivos();
void inicializarUsuariosEjemplo();
int buscarUsuario(String nombre);
void agregarUsuario(String nombre, bool setAsCurrent = true, bool notify = true);
void actualizarPuntuacionUsuario();
void mostrarRanking();
void mostrarResultado();
void verificarRespuesta();
void reiniciarQuiz();
void mostrarTextoEnLineas(String texto, int x, int y, int anchoMax);
String limpiarTexto(String texto);
void iniciarJuego();
void sincronizarUsuariosDesdeAPI();
void postPuntajeToAPI(String nombre, int puntaje);
int readEncoderStep();
int readEncoderEdgeStep();
void gatherRetainedUsuarios(unsigned long timeoutMs);
bool cargarUsuariosDesdeSPIFFS();
bool cargarPreguntasDesdeSPIFFS();
bool buttonPressed();
void actualizarPantallaSegunEstado();

void sincronizarUsuariosDesdeAPI() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String("http://") + String(API_HOST) + String(":8000/accion/ranking");
  // Ajustá API_HOST si tu API corre en otra máquina/host

  Serial.print("🔎 Sincronizando usuarios desde API: "); Serial.println(url);
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    Serial.println("📨 Respuesta API: ");
    Serial.println(payload);

    // parse simple: buscar nombres (JSON)
    DynamicJsonDocument doc(2048);
    auto err = deserializeJson(doc, payload);
    if (!err) {
      JsonArray arr = doc["ranking"].as<JsonArray>();
      for (JsonVariant v : arr) {
        String nombre = v["nombre"].as<String>();
        nombre.trim();
        nombre.toUpperCase();
        if (buscarUsuario(nombre) == -1) {
                // agregar sin auto-seleccionar ni notificar (proveniente de API)
                agregarUsuario(nombre, false, false);
              }
      }
      Serial.println("✅ Sincronizacion completa");
      mostrarPantallaSeleccionUsuario();
    } else {
      Serial.print("❌ Error parseando JSON: "); Serial.println(err.c_str());
    }
  } else {
    Serial.print("❌ Error en GET usuarios, code="); Serial.println(code);
  }
  http.end();
}

// ==================== FUNCIONES MQTT ====================

void callbackMQTT(char *topic, byte *payload, unsigned int length) {
  Serial.println("=== 📨 MQTT CALLBACK ===");
  String top = String(topic);
  String mensaje = "";
  for (int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }
  mensaje.trim();  // 🔹 ELIMINA ESPACIOS O SALTOS DE LÍNEA
  Serial.print("Mensaje en topic '"); Serial.print(top); Serial.print("' -> '"); Serial.print(mensaje); Serial.println("'");

  // Procesamiento en el tópico de acciones (legacy): 'usuario:' y 'usuario_created:'

  if (mensaje == "iniciar_partida") {
    Serial.println("🎮 Comando: iniciar_partida - PROCESANDO");
    // Si ya tenemos modo y usuario seleccionado -> iniciar el juego
    if (modoPartida != "" && usuarioActual >= 0 && usuarioActual < totalUsuarios) {
      Serial.println("✅ Modo y usuario presentes - iniciando juego");
      iniciarJuego();
    } else if (modoPartida == "") {
      // Pedir selección de modo si no está definido
      Serial.println("⚠️ No hay modo seleccionado - mostrando selección de modo");
      estadoActual = ESTADO_SELECCION_MODO;
      opcionSeleccionada = 0;
      mostrarPantallaSeleccionModo();
    } else {
      // Usuario no seleccionado: pedir selección/creación
      Serial.println("⚠️ No hay usuario seleccionado - mostrando selección de usuario");
      estadoActual = ESTADO_SELECCION_USUARIO;
      opcionSeleccionada = 0;
      mostrarPantallaSeleccionUsuario();
    }
  } 
  else if (mensaje.startsWith("modo:")) {
    String nuevoModo = mensaje.substring(5);
    nuevoModo.trim();
    nuevoModo.toLowerCase();

    Serial.print("🎯 Modo recibido: '");
    Serial.print(nuevoModo);
    Serial.println("'");

    if (nuevoModo == "1vs1" || nuevoModo == "ranking" || nuevoModo == "multijugador") {
      modoPartida = nuevoModo;
      Serial.println("✅ Modo establecido: " + modoPartida);
      // Si ya hay usuario seleccionado, ir a pantalla de espera para iniciar
      if (usuarioActual >= 0) {
        estadoActual = ESTADO_ESPERA_INICIO;
        opcionSeleccionada = usuarioActual;
        mostrarPantallaEsperaInicio();
        Serial.println("✅ Modo establecido y usuario ya seleccionado -> pantalla de espera");
      } else {
        estadoActual = ESTADO_SELECCION_USUARIO;
        opcionSeleccionada = 0;
        mostrarPantallaSeleccionUsuario();
        Serial.println("✅ Cambiado a pantalla de selección de usuario");
      }
    } else {
      Serial.println("⚠️ Modo no válido");
    }
  }

  else if (mensaje.startsWith("usuario:")) {
    // 'usuario:' retained message -> only add the user to the list, do NOT auto-select
    String nombre = mensaje.substring(8);
    nombre.trim();
    nombre.toUpperCase();

    Serial.print("👤 Usuario MQTT (retained) recibido: "); Serial.println(nombre);

    int existente = buscarUsuario(nombre);
    if (existente == -1) {
      // Agregado por mensaje retained -> no auto-seleccionar, no notificar
      agregarUsuario(nombre, false, false);
      Serial.println("✅ Usuario agregado vía MQTT (retained): " + nombre);
    } else {
      Serial.println("ℹ️ Usuario ya existía (retained): " + nombre);
    }
    // Do not change selection on retained user messages to avoid unintended auto-selection.
  }

  else if (mensaje.startsWith("usuario_created:")) {
    // Event published only at creation time (non-retained). Auto-select and go to wait state.
    String nombre = mensaje.substring(16);
    nombre.trim();
    nombre.toUpperCase();

    Serial.print("👤 Usuario creado (evento) recibido: "); Serial.println(nombre);

    int existente = buscarUsuario(nombre);
    if (existente == -1) {
      // Evento de creación: agregar y sí notificar/seleccionar
      agregarUsuario(nombre, true, true);
      existente = buscarUsuario(nombre);
      Serial.println("✅ Usuario agregado por evento: " + nombre);
    }
    // Auto-select newly created user and go to wait-to-start
    usuarioActual = existente;
    estadoActual = ESTADO_ESPERA_INICIO;
    opcionSeleccionada = usuarioActual >= 0 ? usuarioActual : 0;
    mostrarPantallaEsperaInicio();
    Serial.println("✅ Usuario auto-seleccionado por evento: " + nombre);
  }

  Serial.println("=== FIN CALLBACK ===");
}


void reconnectMQTT()
{
  while (!mqttClient.connected())
  {
    Serial.print("🔌 Conectando a MQTT...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str()))
    {
      Serial.println("✅ Conectado MQTT!");

    bool subscripcionExitosa = mqttClient.subscribe("wokwi/acciones", 1);
    Serial.print("📡 Suscrito a wokwi/acciones: ");
    Serial.println(subscripcionExitosa ? "ÉXITO" : "FALLÓ");

      mqttClient.publish("wokwi/acciones", "ESP32 conectado");
      // Sincronizar usuarios desde la API una vez que estemos conectados a MQTT
      sincronizarUsuariosDesdeAPI();
    }
    else
    {
      Serial.print("❌ Falló MQTT, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" reintento en 5s");
      delay(5000);
    }
  }
}

// ==================== FUNCIONES TELEGRAM ====================

void conectarTelegram()
{
  Serial.println("📶 Conectando a WiFi...");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Conectando WiFi...");
  display.display();

  WiFi.begin(ssid, password);

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20)
  {
    delay(1000);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n✅ Conectado a WiFi!");
    Serial.print("📡 IP: ");
    Serial.println(WiFi.localIP());

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callbackMQTT);

  // Para Wokwi: permitir TLS sin CA durante pruebas
  secureClient.setInsecure();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Conectando Telegram...");
    display.display();

  int botUsuarios = bot.getUpdates(0);
    if (botUsuarios != -1)
    {
      telegramConnected = true;
      Serial.println("✅ Conexión con Telegram exitosa!");

      bot.sendMessage(CHAT_ID, "🤖 ¡Bot del Quiz ESP32 conectado! 🎮", "");
      bot.sendMessage(CHAT_ID, "Usa /seleccionar_partida para elegir modo de juego", "");

      // MOSTRAR PANTALLA DE INICIO INMEDIATAMENTE
      estadoActual = ESTADO_INICIO;
      mostrarPantallaConexiones();
    }
    else
    {
      Serial.println("❌ Error en conexión Telegram");
      estadoActual = ESTADO_INICIO;
      mostrarPantallaConexiones();
    }
  }
  else
  {
    Serial.println("\n❌ Error conectando a WiFi");
    estadoActual = ESTADO_INICIO;
    mostrarPantallaConexiones();
  }
}

void procesarComandosTelegram()
{
  Serial.println("⏳ Revisando Telegram...");
  if (!telegramConnected) return;

  int numNewMessages = bot.getUpdates(0);
  Serial.print("📬 Mensajes nuevos: ");
  Serial.println(numNewMessages);


  while (numNewMessages)
  {
    for (int i = 0; i < numNewMessages; i++)
    {
      // Raw values para debug (no los normalizamos todavía)
      String raw_chat_id = bot.messages[i].chat_id;
      String raw_text = bot.messages[i].text;

      // Mostrar en Serial exactamente lo que llegó (útil para entender por qué no entra)
      Serial.println("────────────────────────────────────");
      Serial.print("📥 Mensaje index: "); Serial.println(i);
      Serial.print("chat_id(raw): "); Serial.println(raw_chat_id);
      Serial.print("texto(raw): '"); Serial.print(raw_text); Serial.println("'");

      // Normalizamos el texto para comparar (minúscula + trim)
      String text = raw_text;
      text.trim();            // quita espacios al inicio/final
      // si el usuario escribió sin la barra inicial, la toleramos:
      if (!text.startsWith("/")) {
        text = "/" + text;
      }
      text.toLowerCase();

      // Más debug
      Serial.print("texto(normalizado): '"); Serial.print(text); Serial.println("'");
      Serial.print("CHAT_ID esperado: "); Serial.println(CHAT_ID);

  // Para debugging, podemos aceptar cualquier chat durante pruebas.
  bool aceptarCualquierChat = true; // PRUEBAS EN WOKWI: aceptar mensajes desde cualquier chat

  // Comparación numérica de chat_id para evitar diferencias de formato (strings, comillas, etc.)
  long chat_id_rec = raw_chat_id.toInt();
  long chat_id_conf = CHAT_ID.toInt();
  bool mismo_chat = (chat_id_rec == chat_id_conf) || aceptarCualquierChat;

      Serial.print("🧾 chat_id recibido: ");
      Serial.println(raw_chat_id);
      Serial.print("🔑 CHAT_ID configurado: ");
      Serial.println(CHAT_ID);


      // Si necesitás pruebas rápidas, comentá la siguiente línea y pon misma_chat = true;
      // mismo_chat = true;

      if (!mismo_chat) {
        Serial.println("⚠️ Mensaje ignorado: chat distinto.");
        continue;
      }

      // --- ACK automático para pruebas: confirmar recepción del mensaje ---
      if (telegramConnected) {
        String ack = "✅ Recibido: ";
        ack += raw_text;
        bot.sendMessage(raw_chat_id, ack, "");
        Serial.print("✅ Ack enviado: "); Serial.println(raw_text);
        Serial.print("last_message_received: "); Serial.println(bot.last_message_received);
      }

      // --- comandos ---
      if (text == "/start" || text == "/help")
      {
        String welcome = "🎮 *Quiz ESP32 - Comandos disponibles:*\n";
        welcome += "/seleccionar_partida 1vs1\n";
        welcome += "/seleccionar_partida ranking\n";
        welcome += "/seleccionar_partida multijugador\n";
        welcome += "/iniciar_partida\n";
        welcome += "/ingresar_usuario NOMBRE\n";
        welcome += "/ranking\n";
        welcome += "/estado\n";
        bot.sendMessage(raw_chat_id, welcome, "Markdown");
        Serial.println("✅ Respondí /help");
      }
      else if (text == "/estado")
      {
        String estado = "📊 *Estado Actual:*\n";
        estado += "Modo: " + (modoPartida == "" ? "No seleccionado" : modoPartida) + "\n";
        estado += "Estado: ";
        switch (estadoActual)
        {
          case ESTADO_INICIO: estado += "Inicio"; break;
          case ESTADO_SELECCION_MODO: estado += "Seleccionando modo"; break;
          case ESTADO_SELECCION_USUARIO: estado += "Seleccionando usuario"; break;
          case ESTADO_JUGANDO: estado += "Jugando"; break;
          case ESTADO_FINAL: estado += "Finalizado"; break;
        }
        estado += "\nPregunta: " + String(preguntaActual + 1) + "/" + String(totalPreguntas);
        estado += "\nPuntuación: " + String(puntuacionTotal);
        estado += "\nUsuario: " + (usuarioActual >= 0 ? usuarios[usuarioActual].nombre : "No seleccionado");
        bot.sendMessage(raw_chat_id, estado, "Markdown");
        Serial.println("✅ Respondí /estado");
      }
      else if (text == "/ranking")
      {
        String rankingMsg = "🏆 *Ranking Actual:*\n";
        if (totalUsuarios == 0) {
          rankingMsg += "No hay usuarios registrados";
        } else {
          for (int k = 0; k < (totalUsuarios < 5 ? totalUsuarios : 5); k++) {
            rankingMsg += String(k + 1) + ". " + usuarios[k].nombre + " - " + String(usuarios[k].puntuacionMaxima) + " pts\n";
          }
        }
        bot.sendMessage(raw_chat_id, rankingMsg, "Markdown");
        Serial.println("✅ Respondí /ranking");
      }
      else if (text.startsWith("/seleccionar_partida"))
      {
        // extraer modo dinámicamente
        String modo = text.substring(String("/seleccionar_partida").length());
        modo.trim();
        // por si el usuario escribió con o sin slash, o con mayúsculas
        modo.toLowerCase();

        Serial.print("🎯 Modo pedido: '"); Serial.print(modo); Serial.println("'");

        if (modo.length() == 0)
        {
          String ayuda = "🎮 *Selecciona modo de partida:*\n";
          ayuda += "/seleccionar_partida 1vs1\n";
          ayuda += "/seleccionar_partida ranking\n";
          ayuda += "/seleccionar_partida multijugador";
          bot.sendMessage(raw_chat_id, ayuda, "Markdown");
        }
        else if (modo == "1vs1" || modo == "ranking" || modo == "multijugador")
        {
          modoPartida = modo;
          bot.sendMessage(raw_chat_id, "✅ Modo seleccionado: *" + modo + "*", "Markdown");
          Serial.println("📤 Modo guardado localmente: " + modo);

          if (mqttClient.connected()) {
            mqttClient.publish("wokwi/acciones", ("modo:" + modo).c_str());
            Serial.println("📤 Modo enviado por MQTT: " + modo);
          }

          // cuando se selecciona modo por Telegram, pasar a selección usuario
          estadoActual = ESTADO_SELECCION_USUARIO;
          opcionSeleccionada = 0;
          mostrarPantallaSeleccionUsuario();
          Serial.println("➡️ Estado -> ESTADO_SELECCION_USUARIO (por Telegram)");
        }
        else
        {
          bot.sendMessage(raw_chat_id, "❌ Modos válidos: 1vs1, ranking, multijugador", "Markdown");
        }
      }
      else if (text == "/iniciar_partida")
      {
        if (modoPartida == "")
        {
          bot.sendMessage(raw_chat_id, "❌ Primero selecciona un modo con /seleccionar_partida", "Markdown");
        }
        else if (estadoActual != ESTADO_JUGANDO && estadoActual != ESTADO_FINAL)
        {
          bot.sendMessage(raw_chat_id, "🎮 Iniciando partida en modo: *" + modoPartida + "*", "Markdown");
          if (mqttClient.connected())
          {
            mqttClient.publish("wokwi/acciones", "iniciar_partida");
            Serial.println("📤 Comando enviado por MQTT: iniciar_partida");
          }
          // iniciarJuego() se disparará desde el callbackMQTT que ya maneja "iniciar_partida"
        }
        else
        {
          bot.sendMessage(raw_chat_id, "⚠️ El juego ya está en curso", "Markdown");
        }
      }
      else if (text.startsWith("/ingresar_usuario"))
      {
        String nombre = text.substring(String("/ingresar_usuario").length());
        nombre.trim();
        if (nombre.length() == 0) {
          bot.sendMessage(raw_chat_id, "❌ Ingresa un nombre: /ingresar_usuario Sofi", "Markdown");
          Serial.println("❌ Nombre vacío en /ingresar_usuario");
        } else {
          // normalizamos a mayúsculas para almacenamiento y búsqueda consistente
          nombre.toUpperCase();
          Serial.println("📝 Intentando crear/seleccionar usuario: " + nombre);
          int usuarioExistente = buscarUsuario(nombre);

          if (usuarioExistente == -1)
          {
            // creación vía Telegram: agregar, seleccionar y notificar
            agregarUsuario(nombre, true, true);
            bot.sendMessage(raw_chat_id, "✅ Usuario registrado: *" + nombre + "*", "Markdown");
            Serial.println("✅ Usuario creado: " + nombre);
            // seleccionar automáticamente al nuevo usuario
            usuarioActual = buscarUsuario(nombre); // índice recién agregado
            opcionSeleccionada = usuarioActual >= 0 ? usuarioActual : 0;
            // Si el modo es 1vs1, ir directamente a selección de oponente
            if (modoPartida == "1vs1") {
              estadoActual = ESTADO_SELECCION_OPONENTE;
              // empezar la selección de oponente desde el primer distinto
              opcionSeleccionada = (usuarioActual == 0 && totalUsuarios>1) ? 1 : 0;
              mostrarPantallaSeleccionOponente();
            } else {
              estadoActual = ESTADO_ESPERA_INICIO;
              mostrarPantallaEsperaInicio();
            }

            bot.sendMessage(raw_chat_id, "✅ Usuario creado: *" + nombre + "*. Gira el encoder o usa /iniciar_partida para comenzar.", "Markdown");
          }
          else
          {
            usuarioActual = usuarioExistente;
            bot.sendMessage(raw_chat_id, "✅ Usuario seleccionado: *" + nombre + "*", "Markdown");
            Serial.println("🔁 Usuario seleccionado índice: " + String(usuarioActual));
            // En lugar de forzar selección en pantalla, vamos a pantalla de espera
            estadoActual = ESTADO_ESPERA_INICIO;
            opcionSeleccionada = usuarioActual;
            mostrarPantallaEsperaInicio();

            if (modoPartida != "") {
              // Si el modo ya fue seleccionado, iniciamos la partida automáticamente
              bot.sendMessage(raw_chat_id, "🎮 Iniciando partida automáticamente...", "Markdown");
              iniciarJuego();
            } else {
              bot.sendMessage(raw_chat_id, "💡 Usuario listo. Selecciona modo con /seleccionar_partida o gira el encoder para comenzar cuando el modo esté listo.", "Markdown");
            }
          }
        }
      }
      else
      {
        // Comando no reconocido -> mostrar debug al usuario y al serial
        Serial.println("❓ Comando no reconocido: " + text);
        bot.sendMessage(raw_chat_id, "❓ Comando no reconocido. Usa /help para ver comandos.", "Markdown");
      }

    } // for i

  // pedir siguientes mensajes (si hay)
  numNewMessages = bot.getUpdates(0);
  } // while
}

// ==================== NUEVAS FUNCIONES DE PANTALLA ====================

void mostrarPantallaConexiones()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("✅ SISTEMA LISTO");
  display.setCursor(0, 12);
  display.println("WiFi: CONECTADO");
  display.setCursor(0, 24);
  display.println("Telegram: OK");
  display.setCursor(0, 36);
  display.println("MQTT: CONECTADO");
  display.setCursor(0, 48);
  display.println("/iniciar_partida");
  display.setCursor(0, 56);
  display.println("o Gira encoder->");
  display.display();
}

void mostrarPantallaSeleccionModo()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SELECCIONA MODO:");

  String opciones[] = {"1vs1", "Ranking", "Multijugador"};
  for (int i = 0; i < 3; i++)
  {
    display.setCursor(5, 15 + i * 12);
    if (i == opcionSeleccionada)
    {
      display.print("> ");
    }
    else
    {
      display.print("  ");
    }
    display.println(opciones[i]);
  }

  display.setCursor(0, 55);
  display.println("Click: Seleccionar");
  display.display();
}

void mostrarPantallaSeleccionUsuario()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SELECCIONA USUARIO:");

  Serial.print("🔎 Mostrar pantalla selección usuario - totalUsuarios="); Serial.println(totalUsuarios);
  Serial.print("⏱ UI populated at millis: "); Serial.println(millis());
  if (totalUsuarios>0) {
    Serial.println("Lista completa de usuarios:");
    for (int i=0;i<totalUsuarios;i++) {
      Serial.print("  "); Serial.print(i); Serial.print(": "); Serial.println(usuarios[i].nombre);
    }
  }

  if (totalUsuarios == 0)
  {
    display.setCursor(5, 20);
    display.println("No hay usuarios");
    display.setCursor(5, 32);
    display.println("Usa Telegram:");
    display.setCursor(5, 44);
    display.println("/ingresar_usuario");
    display.setCursor(5, 56);
    display.println("para crear uno");
  }
  else
  {
    // MOSTRAR TODOS LOS USUARIOS CON SCROLL
    int inicio = 0;
    int usuariosPorPantalla = 4;

    // Si hay más de 4 usuarios, permitir scroll
    if (totalUsuarios > usuariosPorPantalla)
    {
      inicio = (opcionSeleccionada / usuariosPorPantalla) * usuariosPorPantalla;
    }

    for (int i = 0; i < usuariosPorPantalla && (inicio + i) < totalUsuarios; i++)
    {
      int indiceUsuario = inicio + i;
      display.setCursor(5, 15 + i * 12);
      if (indiceUsuario == opcionSeleccionada)
      {
        display.print("> ");
      }
      else
      {
        display.print("  ");
      }
      display.println(usuarios[indiceUsuario].nombre);
    }

    // Indicador de scroll si hay más usuarios
    if (totalUsuarios > usuariosPorPantalla)
    {
      display.setCursor(110, 55);
      display.print((inicio / usuariosPorPantalla) + 1);
      display.print("/");
      display.print((totalUsuarios + usuariosPorPantalla - 1) / usuariosPorPantalla);
    }

    display.setCursor(0, 55);
    display.println("Click: Seleccionar");
  }
  display.display();
}

void mostrarPantallaEsperaInicio()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LISTO PARA INICIAR");
  display.setCursor(0, 12);
  if (usuarioActual >= 0)
  {
    Serial.print("➡️ Entrando a ESPERA_INICIO - usuarioActual="); Serial.println(usuarioActual);
    display.print("Jugador: ");
    display.println(usuarios[usuarioActual].nombre);
  }
  display.setCursor(0, 28);
  display.println("Gira el encoder ->");
  display.setCursor(0, 40);
  display.println("o /iniciar_partida");
  display.setCursor(0, 56);
  if (modoPartida == "1vs1") {
    display.println("Click: Seleccionar oponente");
  } else {
    display.println("Click: Iniciar");
  }
  display.display();
}

void mostrarPantallaSeleccionOponente()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SELECC. OPONENTE:");

  Serial.print("🔎 Entrando a SELECCION_OPONENTE - totalUsuarios="); Serial.println(totalUsuarios);
  Serial.print("    usuarioActual="); Serial.println(usuarioActual);

  if (totalUsuarios <= 1)
  {
    display.setCursor(5, 20);
    display.println("No hay otros usuarios");
    display.setCursor(5, 36);
    display.println("Crea mas en Telegram");
    display.setCursor(5, 56);
    display.println("Click: Volver");
    display.display();
    return;
  }

  int inicio = 0;
  int usuariosPorPantalla = 4;
  if (totalUsuarios > usuariosPorPantalla)
  {
    inicio = (opcionSeleccionada / usuariosPorPantalla) * usuariosPorPantalla;
  }

  int mostrarIdx = 0;
  for (int i = 0; mostrarIdx < usuariosPorPantalla && (inicio + i) < totalUsuarios; i++)
  {
    int idx = inicio + i;
    if (idx == usuarioActual) continue; // no mostrar al propio jugador

    display.setCursor(5, 15 + mostrarIdx * 12);
    if (idx == opcionSeleccionada)
    {
      display.print("> ");
    }
    else
    {
      display.print("  ");
    }
    display.println(usuarios[idx].nombre);
    mostrarIdx++;
  }

  display.setCursor(0, 55);
  display.println("Click: Seleccionar");
  display.display();
}

// ==================== FUNCIONES EXISTENTES DEL QUIZ ====================

void simularCargaArchivos()
{
  Serial.println("=== SIMULANDO SISTEMA DE ARCHIVOS ===");
  Serial.println("Cargando preguntas desde 'preguntas.txt'...");

  for (int i = 0; i < totalPreguntas; i++)
  {
    String linea = ARCHIVO_PREGUNTAS[i];

    int separadores[5];
    int sepIndex = 0;

    for (int j = 0; j < linea.length() && sepIndex < 5; j++)
    {
      if (linea.charAt(j) == ';')
      {
        separadores[sepIndex++] = j;
      }
    }

    if (sepIndex == 4)
    {
      preguntas[i].texto = linea.substring(0, separadores[0]);
      preguntas[i].opciones[0] = linea.substring(separadores[0] + 1, separadores[1]);
      preguntas[i].opciones[1] = linea.substring(separadores[1] + 1, separadores[2]);
      preguntas[i].opciones[2] = linea.substring(separadores[2] + 1, separadores[3]);
      preguntas[i].respuestaCorrecta = linea.substring(separadores[3] + 1).toInt();
      preguntas[i].puntaje = ARCHIVO_PUNTAJES[i];

      Serial.println("Cargada: " + preguntas[i].texto);
    }
  }
  Serial.println("Total: " + String(totalPreguntas) + " preguntas cargadas\n");
}

void inicializarUsuariosEjemplo()
{
  // ELIMINAR USUARIOS DE PRUEBA - empezar con array vacío
  totalUsuarios = 0;

  Serial.println("Sistema de usuarios inicializado - vacío");
}

int buscarUsuario(String nombre) {
  Serial.print("🔍 Buscando usuario: ");
  Serial.println(nombre);
  
  for(int i = 0; i < totalUsuarios; i++) {
    Serial.print("Comparando con: ");
    Serial.println(usuarios[i].nombre);
    
    if(usuarios[i].nombre == nombre) {
      Serial.print("✅ Usuario encontrado en índice: ");
      Serial.println(i);
      return i;
    }
  }
  Serial.println("❌ Usuario no encontrado");
  return -1;
}

void agregarUsuario(String nombre, bool setAsCurrent, bool notify) {
  if (totalUsuarios < 20) {
    Serial.print("🔧 Agregando usuario: ");
    Serial.println(nombre);

    int nuevoIndice = totalUsuarios;
    usuarios[nuevoIndice].nombre = nombre;
    usuarios[nuevoIndice].puntuacionMaxima = 0;
    usuarios[nuevoIndice].partidasJugadas = 0;

    // Solo establecer usuarioActual si se solicita (ej. creación por UI/Telegram)
    if (setAsCurrent) {
      usuarioActual = nuevoIndice;
    }

    totalUsuarios++;

    Serial.print("✅ Usuario agregado. Total: ");
    Serial.println(totalUsuarios);
    Serial.print("Usuario actual (si aplicado): ");
    Serial.println(usuarioActual);
  Serial.print("⏱ Usuario agregado at millis: "); Serial.println(millis());
  Serial.print("📥 Último usuario: "); Serial.println(usuarios[totalUsuarios-1].nombre);

    // DEBUG simplificado
    Serial.println("=== USUARIOS ACTUALES ===");
    for(int i = 0; i < totalUsuarios; i++) {
      Serial.print(i);
      Serial.print(": ");
      Serial.println(usuarios[i].nombre);
    }
    Serial.println("========================");

    // Notificar por Telegram solo si estamos conectados y notify==true
    if (notify && telegramConnected) {
      bot.sendMessage(CHAT_ID, "👤 Nuevo usuario registrado: *" + nombre + "*", "Markdown");
    }
  } else {
    Serial.println("❌ No se puede agregar usuario - array lleno");
  }
}
void actualizarPuntuacionUsuario()
{
  if (usuarioActual >= 0)
  {
    usuarios[usuarioActual].partidasJugadas++;
    if (puntuacionTotal > usuarios[usuarioActual].puntuacionMaxima)
    {
      usuarios[usuarioActual].puntuacionMaxima = puntuacionTotal;
    }
    Serial.println("Usuario " + usuarios[usuarioActual].nombre +
                   " actualizado. Max: " + usuarios[usuarioActual].puntuacionMaxima +
                   " Partidas: " + usuarios[usuarioActual].partidasJugadas);

    if (telegramConnected)
    {
      String msg = "🏆 *Actualización de Usuario:*\n";
      msg += "Jugador: " + usuarios[usuarioActual].nombre + "\n";
      msg += "Puntuación máxima: " + String(usuarios[usuarioActual].puntuacionMaxima) + "\n";
      msg += "Partidas jugadas: " + String(usuarios[usuarioActual].partidasJugadas);
      bot.sendMessage(CHAT_ID, msg, "Markdown");
    }
  }
}

void mostrarRanking()
{
  Serial.println("=== RANKING ACTUAL ===");

  Usuario rankingTemp[totalUsuarios];
  for (int i = 0; i < totalUsuarios; i++)
  {
    rankingTemp[i] = usuarios[i];
  }

  for (int i = 0; i < totalUsuarios - 1; i++)
  {
    for (int j = i + 1; j < totalUsuarios; j++)
    {
      bool debeIntercambiar = false;

      if (rankingTemp[j].puntuacionMaxima > rankingTemp[i].puntuacionMaxima)
      {
        debeIntercambiar = true;
      }
      else if (rankingTemp[j].puntuacionMaxima == rankingTemp[i].puntuacionMaxima)
      {
        if (rankingTemp[j].partidasJugadas > rankingTemp[i].partidasJugadas)
        {
          debeIntercambiar = true;
        }
        else if (rankingTemp[j].partidasJugadas == rankingTemp[i].partidasJugadas)
        {
          if (rankingTemp[j].nombre < rankingTemp[i].nombre)
          {
            debeIntercambiar = true;
          }
        }
      }

      if (debeIntercambiar)
      {
        Usuario temp = rankingTemp[i];
        rankingTemp[i] = rankingTemp[j];
        rankingTemp[j] = temp;
      }
    }
  }

  for (int i = 0; i < totalUsuarios; i++)
  {
    String posicion = String(i + 1) + ".";
    if (i < 9)
      posicion = " " + posicion;

    Serial.println(posicion + " " + rankingTemp[i].nombre +
                   " - Puntos: " + rankingTemp[i].puntuacionMaxima +
                   " - Partidas: " + rankingTemp[i].partidasJugadas);
  }
  Serial.println("=====================");
}

void mostrarTextoEnLineas(String texto, int x, int y, int anchoMax)
{
  int inicio = 0;
  int fin = anchoMax / 6;

  while (inicio < texto.length())
  {
    if (fin > texto.length())
      fin = texto.length();

    int ultimoEspacio = texto.lastIndexOf(' ', fin);
    if (ultimoEspacio == -1 || ultimoEspacio <= inicio)
      ultimoEspacio = fin;

    display.setCursor(x, y);
    display.println(texto.substring(inicio, ultimoEspacio));

    y += 10;
    inicio = ultimoEspacio + 1;
    fin = inicio + (anchoMax / 6);
  }
}

String limpiarTexto(String texto)
{
  texto.replace("á", "a");
  texto.replace("é", "e");
  texto.replace("í", "i");
  texto.replace("ó", "o");
  texto.replace("ú", "u");
  texto.replace("ñ", "n");
  texto.replace("¿", "?");
  texto.replace("¡", "!");
  return texto;
}

void mostrarPregunta()
{
  display.clearDisplay();

  display.setCursor(0, 0);
  if (usuarioActual >= 0)
  {
    display.print(usuarios[usuarioActual].nombre);
  }
  // Mostrar oponente si es 1vs1
  if (modoPartida == "1vs1" && oponenteActual >= 0)
  {
    display.setCursor(70, 0);
    display.print("VS:");
    display.setCursor(88, 0);
    display.print(usuarios[oponenteActual].nombre);
  }
  else
  {
    display.print("JUGADOR");
  }
  display.setCursor(70, 0);
  display.print(preguntaActual + 1);
  display.print("/");
  display.print(totalPreguntas);
  display.setCursor(100, 0);
  display.print(puntuacionTotal);

  mostrarTextoEnLineas(limpiarTexto(preguntas[preguntaActual].texto), 0, 12, 128);

  for (int i = 0; i < 3; i++)
  {
    display.setCursor(5, 25 + i * 12);
    if (i == opcionSeleccionada)
    {
      display.print("> ");
    }
    else
    {
      display.print("  ");
    }
    display.println(preguntas[preguntaActual].opciones[i]);
  }

  display.display();
}

void mostrarResultado()
{
  display.clearDisplay();
  display.setCursor(20, 10);
  display.println("QUIZ COMPLETADO!");
  display.setCursor(10, 25);
  if (usuarioActual >= 0)
  {
    display.print("Jugador: ");
    display.println(usuarios[usuarioActual].nombre);
  }
  display.setCursor(30, 40);
  display.print("Puntos: ");
  display.println(puntuacionTotal);

  // Si es 1vs1 mostrar resumen contra oponente
  if (modoPartida == "1vs1" && oponenteActual >= 0)
  {
    display.setCursor(0, 50);
    String s = "VS "; s += usuarios[oponenteActual].nombre;
    display.println(s);
    display.setCursor(80, 50);
    display.print("O: "); display.print(usuarios[oponenteActual].puntuacionMaxima);
  }
  display.setCursor(5, 55);
  display.println("Click para reiniciar");
  display.display();
}

void verificarRespuesta()
{
  bool correcta = (opcionSeleccionada == preguntas[preguntaActual].respuestaCorrecta);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(preguntas[preguntaActual].texto);
  display.setCursor(0, 15);
  display.print("Elegiste: ");
  display.println(preguntas[preguntaActual].opciones[opcionSeleccionada]);

  if (correcta)
  {
    puntuacionTotal += preguntas[preguntaActual].puntaje;
    display.setCursor(0, 30);
    display.print("CORRECTO! +");
    display.print(preguntas[preguntaActual].puntaje);
    digitalWrite(LED_PIN, HIGH);

    if (telegramConnected)
    {
      bot.sendMessage(CHAT_ID, "✅ ¡Respuesta correcta! +" + String(preguntas[preguntaActual].puntaje) + " puntos", "");
    }
  }
  else
  {
    display.setCursor(0, 30);
    display.println("INCORRECTO");
    display.setCursor(0, 45);
    display.print("Correcta: ");
    display.println(preguntas[preguntaActual].opciones[preguntas[preguntaActual].respuestaCorrecta]);

    if (telegramConnected)
    {
      bot.sendMessage(CHAT_ID, "❌ Respuesta incorrecta. La correcta era: " + preguntas[preguntaActual].opciones[preguntas[preguntaActual].respuestaCorrecta], "");
    }
  }

  display.display();
  delay(1500); // REDUCIDO DE 3000ms A 1500ms
  digitalWrite(LED_PIN, LOW);

  preguntaActual++;
  if (preguntaActual >= totalPreguntas)
  {
    quizCompletado = true;
    actualizarPuntuacionUsuario();
    mostrarRanking();
    estadoActual = ESTADO_FINAL;
    mostrarResultado();

    if (telegramConnected)
    {
      String resultado = "🏁 *QUIZ COMPLETADO!*\n";
      resultado += "Jugador: " + (usuarioActual >= 0 ? usuarios[usuarioActual].nombre : "Anónimo") + "\n";
      resultado += "Puntuación final: *" + String(puntuacionTotal) + "* puntos\n";
      resultado += "Modo de juego: " + modoPartida;
      bot.sendMessage(CHAT_ID, resultado, "Markdown");
    }
    // En modo 1vs1, comparar con el oponente y notificar
    if (modoPartida == "1vs1" && oponenteActual >= 0 && oponenteActual < totalUsuarios) {
      String msg;
      int puntOponente = usuarios[oponenteActual].puntuacionMaxima;
      if (puntuacionTotal > puntOponente) {
        msg = "🏆 Resultado 1vs1: GANASTE!\n";
        msg += "Tu puntaje: " + String(puntuacionTotal) + " vs " + usuarios[oponenteActual].nombre + ": " + String(puntOponente);
      } else if (puntuacionTotal == puntOponente) {
        msg = "⚖️ Resultado 1vs1: EMPATE\n";
        msg += "Tu puntaje: " + String(puntuacionTotal) + " vs " + usuarios[oponenteActual].nombre + ": " + String(puntOponente);
      } else {
        msg = "❌ Resultado 1vs1: PERDISTE\n";
        msg += "Tu puntaje: " + String(puntuacionTotal) + " vs " + usuarios[oponenteActual].nombre + ": " + String(puntOponente);
      }
      if (telegramConnected) bot.sendMessage(CHAT_ID, msg, "Markdown");
      // Enviar puntaje del jugador a la API para actualizar ranking
      postPuntajeToAPI(usuarios[usuarioActual].nombre, puntuacionTotal);
    } else {
      // Para otros modos, enviar puntaje del jugador
      if (usuarioActual >= 0) postPuntajeToAPI(usuarios[usuarioActual].nombre, puntuacionTotal);
    }
  }
  else
  {
    opcionSeleccionada = 0;
    mostrarPregunta(); // MOSTRAR SIGUIENTE PREGUNTA INMEDIATAMENTE
  }
}

void iniciarJuego() {
  Serial.println("=== 🎮 INICIAR JUEGO ===");
  
  // VALIDACIONES MÁS ESTRICTAS
  if (usuarioActual < 0) {
    Serial.println("❌ ERROR: No hay usuario seleccionado (usuarioActual < 0)");
    return;
  }
  
  if (usuarioActual >= totalUsuarios) {
    Serial.println("❌ ERROR: usuarioActual fuera de rango");
    return;
  }
  
  if (modoPartida == "") {
    Serial.println("❌ ERROR: No hay modo seleccionado");
    return;
  }
  
  Serial.print("Jugador: ");
  Serial.println(usuarios[usuarioActual].nombre);
  Serial.print("Modo: ");
  Serial.println(modoPartida);
  
  // REINICIAR VARIABLES DEL JUEGO
  quizIniciado = true;
  quizCompletado = false;
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  estadoActual = ESTADO_JUGANDO;
  
  if (telegramConnected) {
    String mensaje = "🎮 Partida iniciada!\n";
    mensaje += "Jugador: *" + usuarios[usuarioActual].nombre + "*\n";
    mensaje += "Modo: *" + modoPartida + "*";
    if (modoPartida == "1vs1" && oponenteActual >= 0) {
      mensaje += "\nOponente: *" + usuarios[oponenteActual].nombre + "*";
    }
    bot.sendMessage(CHAT_ID, mensaje, "Markdown");
  }
  
  mostrarPregunta();
  Serial.println("✅ Juego iniciado correctamente");
}

void reiniciarQuiz()
{
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  quizCompletado = false;
  quizIniciado = false;
  estadoActual = ESTADO_INICIO;

  mostrarPantallaConexiones();

  if (telegramConnected)
  {
    bot.sendMessage(CHAT_ID, "🔄 Quiz reiniciado. Listo para nueva partida", "");
  }
}

void setup()
{
  Serial.begin(9600);

  pinMode(LED_PIN, OUTPUT);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  // Inicializar estado del encoder (usaremos polling por flancos)
  lastEncoderState = (digitalRead(ENCODER_CLK) << 1) | digitalRead(ENCODER_DT);
  lastCLK = digitalRead(ENCODER_CLK);
  lastDT = digitalRead(ENCODER_DT);
  lastButtonState = digitalRead(ENCODER_SW);

  display.begin(0x3c, true);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();

    // mqttClient callback is set when MQTT server is configured (in conectarTelegram)

  // Mostrar pantalla de conexión inmediatamente
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Iniciando sistema...");
  display.display();

  simularCargaArchivos();
  inicializarUsuariosEjemplo(); // Ahora inicia vacío

  conectarTelegram(); // Esta función ahora muestra la pantalla de inicio automáticamente

  // Intentar sincronizar usuarios existentes desde la API (si está corriendo en el host)
  sincronizarUsuariosDesdeAPI();

  Serial.println("🚀 Sistema listo. Flujo mejorado activado.");
}

void loop()
{
  if (!mqttClient.connected())
  {
    reconnectMQTT();
  }
  mqttClient.loop();

  if (millis() > lastTimeBotRan + 100)
  {
    procesarComandosTelegram();
    lastTimeBotRan = millis();
  }

  // Revisión periódica rápida para forzar actualización de pantalla si algo externo cambió
  static unsigned long lastDisplayCheck = 0;
  if (millis() > lastDisplayCheck + 100) {
    lastDisplayCheck = millis();
    if (needsActualizarPantalla) {
      actualizarPantallaSegunEstado();
    }
  }


  int currentCLK = digitalRead(ENCODER_CLK);

  switch (estadoActual)
  {
  case ESTADO_INICIO:
  {
  int step = readEncoderStep();
    if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS)
    {
      lastEncoderStep = millis();
      estadoActual = ESTADO_SELECCION_MODO;
      opcionSeleccionada = 0;
      mostrarPantallaSeleccionModo();
    }
  }
    break;

  case ESTADO_SELECCION_MODO:
    {
  int step = readEncoderStep();
      if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS)
      {
        lastEncoderStep = millis();
        if (step > 0) {
          opcionSeleccionada++;
        } else {
          opcionSeleccionada--;
        }
        if (opcionSeleccionada < 0)
          opcionSeleccionada = 2;
        mostrarPantallaSeleccionModo();
      }
    }

  if (buttonPressed())
    {
      String modos[] = {"1vs1", "ranking", "multijugador"};
      modoPartida = modos[opcionSeleccionada];
      modoSeleccionadoPorTelegram = false;

      estadoActual = ESTADO_SELECCION_USUARIO;
      opcionSeleccionada = 0;
      mostrarPantallaSeleccionUsuario();
    }
    break;

  case ESTADO_SELECCION_USUARIO:
    if (totalUsuarios > 0)
    {
      {
  int step = readEncoderStep();
        if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS)
        {
          lastEncoderStep = millis();
          if (step > 0)
          {
            opcionSeleccionada++;
          }
          else
          {
            opcionSeleccionada--;
          }
          if (opcionSeleccionada < 0)
            opcionSeleccionada = totalUsuarios - 1;
          if (opcionSeleccionada >= totalUsuarios)
            opcionSeleccionada = 0;
          mostrarPantallaSeleccionUsuario();
        }
      }

      if (buttonPressed())
      {
        usuarioActual = opcionSeleccionada;
        // ir a pantalla de espera en vez de comenzar inmediatamente
        estadoActual = ESTADO_ESPERA_INICIO;
        needsActualizarPantalla = true;
        mostrarPantallaEsperaInicio();
      }
    }
    else {
      // Si no hay usuarios, intentar sincronizar desde API/MQTT para poblar la lista
      Serial.println("🔄 No hay usuarios locales - solicitando sincronizacion API/MQTT");
      sincronizarUsuariosDesdeAPI();
      if (mqttClient.connected()) {
        mqttClient.publish("wokwi/acciones", "pedir_usuarios");
        Serial.println("📤 Peticion MQTT: pedir_usuarios");
      }
      mostrarPantallaSeleccionUsuario();
    }
    break;

  case ESTADO_ESPERA_INICIO:
    // En la pantalla de espera, el encoder permite cambiar el usuario visualmente
    if (totalUsuarios > 0)
    {
    {
  int step = readEncoderStep();
        if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS)
        {
          lastEncoderStep = millis();
          if (step > 0)
          {
            opcionSeleccionada++;
          }
          else
          {
            opcionSeleccionada--;
          }
          if (opcionSeleccionada < 0)
            opcionSeleccionada = totalUsuarios - 1;
          if (opcionSeleccionada >= totalUsuarios)
            opcionSeleccionada = 0;
          usuarioActual = opcionSeleccionada;
          mostrarPantallaEsperaInicio();
        }
      }

      if (buttonPressed())
      {
        // si el modo es 1vs1, pasar a seleccionar oponente
        if (modoPartida == "1vs1") {
          estadoActual = ESTADO_SELECCION_OPONENTE;
          // iniciar la opcion de selección desde el primer usuario visible
          opcionSeleccionada = (usuarioActual == 0 && totalUsuarios>1) ? 1 : 0;
          mostrarPantallaSeleccionOponente();
        } else {
          // iniciar partida con el usuario actualmente seleccionado
          iniciarJuego();
        }
      }
    }
    break;

  case ESTADO_SELECCION_OPONENTE:
    if (totalUsuarios > 1)
    {
    {
  int step = readEncoderStep();
        if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS)
        {
          lastEncoderStep = millis();
          if (step > 0)
          {
            opcionSeleccionada++;
          }
          else
          {
            opcionSeleccionada--;
          }
          if (opcionSeleccionada < 0)
            opcionSeleccionada = totalUsuarios - 1;
          if (opcionSeleccionada >= totalUsuarios)
            opcionSeleccionada = 0;
          // saltar propio usuario
          if (opcionSeleccionada == usuarioActual) {
            opcionSeleccionada = (opcionSeleccionada + 1) % totalUsuarios;
          }
          mostrarPantallaSeleccionOponente();
        }
      }

      if (buttonPressed())
      {
        oponenteActual = opcionSeleccionada;
        // una vez seleccionado el oponente iniciamos la partida
        iniciarJuego();
      }
    }
    break;

  case ESTADO_JUGANDO:
    {
    int step = readEncoderStep();
      if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS)
      {
        lastEncoderStep = millis();
        if (step > 0)
        {
          opcionSeleccionada++;
        }
        else
        {
          opcionSeleccionada--;
        }
        if (opcionSeleccionada < 0)
          opcionSeleccionada = 2;
        if (opcionSeleccionada > 2)
          opcionSeleccionada = 0;
        mostrarPregunta();
      }
    }

    if (buttonPressed())
    {
      verificarRespuesta();
    }
    break;

  case ESTADO_FINAL:
    if (buttonPressed() && millis() - lastButtonPress > 500)
    {
      reiniciarQuiz();
    }
    break;
  }

  // previous lastCLK logic removed; encoder handled by readEncoderStep()
}

void postPuntajeToAPI(String nombre, int puntaje) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("http://") + String(API_HOST) + String(":8000/accion/actualizar_puntaje");
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"nombre\": \"" + nombre + "\", \"puntaje\": " + String(puntaje) + "}";
  int code = http.POST(body);
  Serial.print("🔁 POST puntaje "); Serial.print(nombre); Serial.print(" -> code="); Serial.println(code);
  http.end();
}

// (Transition-table encoder implementation removed — using polling/flank detection below)

// Intentar recolectar mensajes retained de usuarios durante timeoutMs
void gatherRetainedUsuarios(unsigned long timeoutMs) {
  unsigned long start = millis();
  Serial.println("🔎 Recolectando retained usuarios por MQTT...");
  while (millis() - start < timeoutMs) {
    mqttClient.loop();
    delay(5);
  }
  Serial.println("🔎 Recolección finalizada");
}

// Polling-only button detection con debounce (Wokwi-friendly)
bool buttonPressed() {
  int current = digitalRead(ENCODER_SW);
  unsigned long now = millis();
  if (current == LOW && lastButtonState == HIGH && (now - lastButtonPress) > BUTTON_DEBOUNCE_MS) {
    lastButtonPress = now;
    lastButtonState = current;
    // bloquear encoder temporalmente para evitar rotaciones accidentales
    encoderEnabled = false;
    encoderDisableUntil = now + 150;
    return true;
  }
  lastButtonState = current;
  if (!encoderEnabled && millis() > encoderDisableUntil) encoderEnabled = true;
  return false;
}

// Detección por flanco simple para el encoder (polling)
int readEncoderStep() {
  if (!encoderEnabled) return 0;
  int a = digitalRead(ENCODER_CLK);
  int b = digitalRead(ENCODER_DT);
  int step = 0;
  if (a != lastCLK) {
    if (a == LOW) {
      // dirección basada en DT
      if (b != a) step = 1; else step = -1;
    }
    lastCLK = a;
  }
  return step;
}

// Forzar la actualización de pantalla según el estado actual
void actualizarPantallaSegunEstado() {
  needsActualizarPantalla = false;
  switch (estadoActual) {
    case ESTADO_INICIO: mostrarPantallaConexiones(); break;
    case ESTADO_SELECCION_MODO: mostrarPantallaSeleccionModo(); break;
    case ESTADO_SELECCION_USUARIO: mostrarPantallaSeleccionUsuario(); break;
    case ESTADO_ESPERA_INICIO: mostrarPantallaEsperaInicio(); break;
    case ESTADO_SELECCION_OPONENTE: mostrarPantallaSeleccionOponente(); break;
    case ESTADO_JUGANDO: mostrarPregunta(); break;
    case ESTADO_FINAL: mostrarResultado(); break;
    default: mostrarPantallaConexiones(); break;
  }
}