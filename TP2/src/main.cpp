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
const char *API_HOST = "192.168.50.111";

// Telegram
const String BOT_TOKEN = "8376405384:AAH_30BV0A7zlZotdfKpx3KucxvUtSanau8";
const String CHAT_ID = "1078301149";

// Separate clients
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

// SIMULACIÓN DE ARCHIVOS
const char *ARCHIVO_PREGUNTAS[] = {
    "¿Qué lenguaje usa Arduino?;C++;Python;Java;0",
    "Capital de Francia;Roma;Madrid;París;2",
    "Color bandera Argentina;Azul;Blanco;Celeste;2",
    "¿2+2?;3;4;5;1",
    "Animal Australia;Canguro;Koala;Emú;0"};

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

// Variables encoder/button
unsigned long lastButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 50;
unsigned long lastEncoderStep = 0;
const unsigned long ENCODER_STEP_MS = 100;
int lastCLK = HIGH;
int lastDT = HIGH;
int lastButtonState = HIGH;
bool encoderEnabled = true;
unsigned long encoderDisableUntil = 0;

// Flag para actualización de pantalla
volatile bool needsActualizarPantalla = true; // Iniciar como true para primera actualización

// Variables Telegram
unsigned long lastTimeBotRan = 0;
bool telegramConnected = false;

// Control de reconexión MQTT
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

// ==================== ESTADOS ====================
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
EstadoSistema ultimoEstado = ESTADO_INICIO;
int ultimaOpcion = -1;

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
void mostrarResultado();
void verificarRespuesta();
void reiniciarQuiz();
void mostrarTextoEnLineas(String texto, int x, int y, int anchoMax);
String limpiarTexto(String texto);
void iniciarJuego();
void sincronizarUsuariosDesdeAPI();
void postPuntajeToAPI(String nombre, int puntaje);
int readEncoderStep();
bool buttonPressed();
void actualizarPantallaSegunEstado();
bool checkMqttConnection();

void sincronizarUsuariosDesdeAPI() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://" + String(API_HOST) + ":8000/accion/ranking";

  Serial.print("Sincronizando usuarios desde API: ");
  Serial.println(url);
  
  http.begin(url);
  http.setTimeout(5000);
  
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();

    DynamicJsonDocument doc(2048);
    auto err = deserializeJson(doc, payload);
    if (!err) {
      JsonArray arr = doc["ranking"].as<JsonArray>();
      for (JsonVariant v : arr) {
        String nombre = v["nombre"].as<String>();
        nombre.trim();
        nombre.toUpperCase();
        if (buscarUsuario(nombre) == -1) {
          agregarUsuario(nombre, false, false);
        }
      }
      Serial.println("Sincronizacion completa");
      needsActualizarPantalla = true;
    }
  } else {
    Serial.print("Error API: ");
    Serial.println(code);
  }
  http.end();
}

// ==================== FUNCIONES MQTT MEJORADAS ====================

void callbackMQTT(char *topic, byte *payload, unsigned int length) {
  String mensaje = "";
  for (int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }
  mensaje.trim();

  Serial.print("MQTT Recibido: ");
  Serial.println(mensaje);

  // Procesar mensaje de iniciar_partida
  if (mensaje == "iniciar_partida") {
    Serial.println("🎮 Comando: iniciar_partida - PROCESANDO");
    
    // Verificar si tenemos los requisitos para iniciar
    if (modoPartida != "" && usuarioActual >= 0) {
      Serial.println("✅ Modo y usuario presentes - iniciando juego");
      iniciarJuego();
    } else {
      Serial.println("⚠️ Faltan requisitos:");
      Serial.print("  - Modo: ");
      Serial.println(modoPartida);
      Serial.print("  - Usuario: ");
      Serial.println(usuarioActual);
      
      // Ir al estado apropiado según lo que falta
      if (modoPartida == "") {
        estadoActual = ESTADO_SELECCION_MODO;
        Serial.println("➡️ Yendo a selección de modo");
      } else {
        estadoActual = ESTADO_SELECCION_USUARIO;
        Serial.println("➡️ Yendo a selección de usuario");
      }
      opcionSeleccionada = 0;
      needsActualizarPantalla = true;
    }
  } 
  // Procesar mensaje de modo
  else if (mensaje.startsWith("modo:")) {
    String nuevoModo = mensaje.substring(5);
    nuevoModo.trim();
    nuevoModo.toLowerCase();

    Serial.print("🎯 Modo recibido: ");
    Serial.println(nuevoModo);

    if (nuevoModo == "1vs1" || nuevoModo == "ranking" || nuevoModo == "multijugador") {
      modoPartida = nuevoModo;
      Serial.println("✅ Modo establecido: " + modoPartida);
      
      // Cambiar estado según situación actual
      if (usuarioActual >= 0) {
        estadoActual = ESTADO_ESPERA_INICIO;
        opcionSeleccionada = usuarioActual;
        Serial.println("✅ Usuario ya seleccionado -> pantalla de espera");
      } else {
        estadoActual = ESTADO_SELECCION_USUARIO;
        opcionSeleccionada = 0;
        Serial.println("✅ Cambiado a selección de usuario");
      }
      needsActualizarPantalla = true;
    } else {
      Serial.println("❌ Modo no válido");
    }
  }
  // Procesar usuario existente (solo agregar)
  else if (mensaje.startsWith("usuario:")) {
    String nombre = mensaje.substring(8);
    nombre.trim();
    nombre.toUpperCase();

    Serial.print("👤 Usuario recibido: ");
    Serial.println(nombre);

    int existente = buscarUsuario(nombre);
    if (existente == -1) {
      agregarUsuario(nombre, false, false);
      Serial.println("✅ Usuario agregado a la lista");
    }
  }
  // Procesar creación de usuario (agregar y seleccionar)
  // En la función callbackMQTT, REEMPLAZA esta parte:
else if (mensaje.startsWith("usuario_created:")) {
    String nombre = mensaje.substring(16);
    nombre.trim();
    nombre.toUpperCase();

    Serial.print("👤 Usuario creado recibido: ");
    Serial.println(nombre);

    int existente = buscarUsuario(nombre);
    
    // ⚡ SIEMPRE seleccionar el usuario, ya sea nuevo o existente
    if (existente == -1) {
        // Si no existe, crearlo
        agregarUsuario(nombre, true, true);
        existente = buscarUsuario(nombre);
        Serial.println("✅ Usuario NUEVO - creado y seleccionado");
    } else {
        // Si ya existe, seleccionarlo directamente
        usuarioActual = existente;
        Serial.println("✅ Usuario EXISTENTE - seleccionado");
    }
    
    // Configurar estado
    estadoActual = ESTADO_ESPERA_INICIO;
    opcionSeleccionada = usuarioActual;
    needsActualizarPantalla = true;
    
    Serial.print("Usuario actual: ");
    Serial.println(usuarioActual);
    Serial.print("Nombre: ");
    Serial.println(usuarios[usuarioActual].nombre);
}
}

// Función mejorada de reconexión MQTT
// REEMPLAZA la función checkMqttConnection con esta versión NO BLOQUEANTE:
bool checkMqttConnection() {
  if (mqttClient.connected()) {
    return true;
  }
  
  unsigned long now = millis();
  if (now - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
    lastMqttReconnectAttempt = now;
    
    Serial.print("Intentando conectar MQTT... ");
    String clientId = "ESP32Quiz-" + String(random(0xffff), HEX);
    
    // ⚡ CONEXIÓN NO BLOQUEANTE con timeout corto
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("conectado!");
      
      // Suscribirse al tópico
      if (mqttClient.subscribe("wokwi/acciones")) {
        Serial.println("Suscripción a wokwi/acciones: OK");
      } else {
        Serial.println("Suscripción a wokwi/acciones: FALLÓ");
      }
      
      return true;
    } else {
      Serial.print("falló, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" intentar again en 5 segundos");
      return false;
    }
  }
  return false;
}

// MEJORA la función reconnectMQTT para que sea no bloqueante:
void reconnectMQTT() {
  // Esta función ahora es llamada de forma no bloqueante en el loop
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    
    // ⚡ SOLO intentar reconexión cada 5 segundos
    if (now - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnectAttempt = now;
      
      Serial.print("🔌 Conectando MQTT...");
      String clientId = "ESP32Client-" + String(random(0xffff), HEX);

      // ⚡ INTENTO RÁPIDO de conexión (no bloqueante)
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println("✅ Conectado MQTT!");
        
        bool subscripcionExitosa = mqttClient.subscribe("wokwi/acciones", 1);
        Serial.print("📡 Suscrito a wokwi/acciones: ");
        Serial.println(subscripcionExitosa ? "ÉXITO" : "FALLÓ");
        
        // Publicar mensaje de conexión
        mqttClient.publish("wokwi/acciones", "ESP32 reconectado");
      } else {
        Serial.print("❌ Falló MQTT, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" reintento en 5s");
        // NO hacer delay aquí - eso bloquearía el loop
      }
    }
  }
}

// ==================== FUNCIONES TELEGRAM ====================

void conectarTelegram()
{
  Serial.println("Conectando a WiFi...");
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
    Serial.println("Conectado a WiFi!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Configurar MQTT una sola vez
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(callbackMQTT);
    mqttClient.setBufferSize(1024); // Aumentar buffer para mensajes largos

    secureClient.setInsecure();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Conectando servicios...");
    display.display();

    // Intentar conectar Telegram
    int botUsuarios = bot.getUpdates(0);
    if (botUsuarios != -1)
    {
      telegramConnected = true;
      Serial.println("Conexion con Telegram exitosa!");
      bot.sendMessage(CHAT_ID, "Quiz ESP32 conectado y listo!", "");
    } else {
      Serial.println("Telegram no disponible, continuando...");
    }

    // Sincronizar usuarios
    sincronizarUsuariosDesdeAPI();
    
    estadoActual = ESTADO_INICIO;
    needsActualizarPantalla = true;
    
  } else {
    Serial.println("Error conectando a WiFi");
    estadoActual = ESTADO_INICIO;
    needsActualizarPantalla = true;
  }
}

void procesarComandosTelegram()
{
  if (!telegramConnected) return;

  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  if (numNewMessages) {
    Serial.print("Mensajes Telegram: ");
    Serial.println(numNewMessages);
  }

  while (numNewMessages)
  {
    for (int i = 0; i < numNewMessages; i++)
    {
      String raw_chat_id = bot.messages[i].chat_id;
      String raw_text = bot.messages[i].text;

      String text = raw_text;
      text.trim();
      if (!text.startsWith("/")) {
        text = "/" + text;
      }
      text.toLowerCase();

      // Solo responder a comandos básicos para no bloquear
      if (text == "/start" || text == "/help") {
        String welcome = "Quiz ESP32 - Comandos:\n";
        welcome += "/ingresar_usuario NOMBRE\n";
        welcome += "/seleccionar_partida MODO\n"; 
        welcome += "/iniciar_partida\n";
        welcome += "/estado\n";
        bot.sendMessage(raw_chat_id, welcome);
      }
      else if (text == "/estado") {
        String estado_msg = "Estado:\nModo: ";
        estado_msg += modoPartida;
        estado_msg += "\nUsuario: ";
        estado_msg += (usuarioActual >= 0 ? usuarios[usuarioActual].nombre : "Ninguno");
        bot.sendMessage(raw_chat_id, estado_msg);
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ==================== FUNCIONES DE PANTALLA ====================

void mostrarPantallaConexiones()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("--SISTEMA LISTO--");
  display.setCursor(0, 12);
  display.println("WiFi: CONECTADO");
  display.setCursor(0, 24);
  if (telegramConnected) {
    display.println("Telegram: OK");
  } else {
    display.println("Telegram: --");
  }
  display.setCursor(0, 36);
  display.println("Usa Telegram:");
  display.setCursor(0, 48);
  display.println("/ingresar_usuario");
  display.setCursor(0, 56);
  display.println("/seleccionar_partida");
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
    int inicio = 0;
    int usuariosPorPantalla = 4;

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
    display.print("Jugador: ");
    display.println(usuarios[usuarioActual].nombre);
  }
  display.setCursor(0, 24);
  display.print("Modo: ");
  display.println(modoPartida);
  display.setCursor(0, 36);
  display.println("Gira encoder ->");
  display.setCursor(0, 48);
  display.println("o /iniciar_partida");
  display.setCursor(0, 56);
  display.println("Click: Iniciar");
  display.display();
}

void mostrarPantallaSeleccionOponente()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SELECC. OPONENTE:");

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
    if (idx == usuarioActual) continue;

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

// REEMPLAZA la función mostrarPregunta con esta versión mejorada:
void mostrarPregunta() {
    display.clearDisplay();

    // Header con información (tamaño normal)
    display.setCursor(0, 0);
    if (usuarioActual >= 0) {
        // Limitar longitud del nombre si es muy largo
        String nombreDisplay = usuarios[usuarioActual].nombre;
        if (nombreDisplay.length() > 8) {
            nombreDisplay = nombreDisplay.substring(0, 8) + ".";
        }
        display.print(nombreDisplay);
    }
    
    if (modoPartida == "1vs1" && oponenteActual >= 0) {
        display.setCursor(65, 0);
        display.print("VS:");
        String oponenteDisplay = usuarios[oponenteActual].nombre;
        if (oponenteDisplay.length() > 6) {
            oponenteDisplay = oponenteDisplay.substring(0, 6) + ".";
        }
        display.setCursor(85, 0);
        display.print(oponenteDisplay);
    }
    
    display.setCursor(105, 0);
    display.print(preguntaActual + 1);
    display.print("/");
    display.print(totalPreguntas);
    
    // ⚡ MEJOR FORMATO PARA PREGUNTAS Y OPCIONES
    String preguntaTexto = preguntas[preguntaActual].texto;
    
    // Corregir caracteres especiales
    preguntaTexto.replace("¿", "?");  // Corregir signo de pregunta invertido
    preguntaTexto.replace("¡", "!");  // Corregir signo de exclamación invertido
    
    // Mostrar pregunta con mejor formato
    int yPos = 12;
    int maxWidth = 120;
    int charWidth = 6; // Ancho aproximado de cada carácter
    
    // Dividir pregunta en líneas si es muy larga
    String lineaActual = "";
    for (int i = 0; i < preguntaTexto.length(); i++) {
        char c = preguntaTexto[i];
        lineaActual += c;
        
        // Si la línea actual excede el ancho o encontramos un espacio después de cierto punto
        if (lineaActual.length() * charWidth >= maxWidth || (c == ' ' && lineaActual.length() * charWidth >= maxWidth - 20)) {
            display.setCursor(0, yPos);
            display.println(lineaActual);
            yPos += 10;
            lineaActual = "";
        }
    }
    
    // Mostrar última línea si queda texto
    if (lineaActual.length() > 0) {
        display.setCursor(0, yPos);
        display.println(lineaActual);
        yPos += 12;
    }
    
    // ⚡ MOSTRAR OPCIONES CON MEJOR ESPACIADO
    for (int i = 0; i < 3; i++) {
        display.setCursor(5, yPos + (i * 10));
        if (i == opcionSeleccionada) {
            display.print(">");
        } else {
            display.print(" ");
        }
        display.print(" ");
        
        // Limitar longitud de opciones si son muy largas
        String opcion = preguntas[preguntaActual].opciones[i];
        if (opcion.length() > 18) {
            opcion = opcion.substring(0, 18) + ".";
        }
        display.println(opcion);
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

// ==================== FUNCIONES DEL QUIZ ====================

void simularCargaArchivos()
{
  Serial.println("=== CARGANDO PREGUNTAS ===");

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
    }
  }
  Serial.println("Preguntas cargadas");
}

void inicializarUsuariosEjemplo()
{
  totalUsuarios = 0;
  Serial.println("Sistema de usuarios inicializado");
}

int buscarUsuario(String nombre) {
  for(int i = 0; i < totalUsuarios; i++) {
    if(usuarios[i].nombre == nombre) {
      return i;
    }
  }
  return -1;
}

void agregarUsuario(String nombre, bool setAsCurrent, bool notify) {
  if (totalUsuarios < 20) {
    usuarios[totalUsuarios].nombre = nombre;
    usuarios[totalUsuarios].puntuacionMaxima = 0;
    usuarios[totalUsuarios].partidasJugadas = 0;

    if (setAsCurrent) {
      usuarioActual = totalUsuarios;
    }

    totalUsuarios++;

    Serial.print("Usuario agregado: ");
    Serial.println(nombre);
    Serial.print("Total usuarios: ");
    Serial.println(totalUsuarios);
    Serial.print("Usuario actual: ");
    Serial.println(usuarioActual);

    if (notify && telegramConnected) {
      bot.sendMessage(CHAT_ID, "Nuevo usuario: " + nombre);
    }
    
    needsActualizarPantalla = true;
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
  }
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

// MEJORA la función limpiarTexto:
String limpiarTexto(String texto) {
    // Caracteres españoles
    texto.replace("á", "a");
    texto.replace("é", "e");
    texto.replace("í", "i");
    texto.replace("ó", "o");
    texto.replace("ú", "u");
    texto.replace("ñ", "n");
    
    // ⚡ CORREGIR SIGNOS ESPAÑOLES (problema del "¿" al revés)
    texto.replace("¿", "?");  // Cambiar ¿ por ?
    texto.replace("¡", "!");  // Cambiar ¡ por !
    
    // Eliminar caracteres problemáticos
    texto.replace("`", "");
    texto.replace("´", "");
    texto.replace("¨", "");
    
    return texto;
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
  }
  else
  {
    display.setCursor(0, 30);
    display.println("INCORRECTO");
    display.setCursor(0, 45);
    display.print("Correcta: ");
    display.println(preguntas[preguntaActual].opciones[preguntas[preguntaActual].respuestaCorrecta]);
  }

  display.display();
  delay(1500);
  digitalWrite(LED_PIN, LOW);

  preguntaActual++;
  if (preguntaActual >= totalPreguntas)
  {
    quizCompletado = true;
    actualizarPuntuacionUsuario();
    estadoActual = ESTADO_FINAL;
    mostrarResultado();

    if (usuarioActual >= 0) {
      postPuntajeToAPI(usuarios[usuarioActual].nombre, puntuacionTotal);
    }
  }
  else
  {
    opcionSeleccionada = 0;
    mostrarPregunta();
  }
}

void iniciarJuego() {
  Serial.println("=== INICIAR JUEGO ===");
  
  if (usuarioActual < 0) {
    Serial.println("ERROR: No hay usuario seleccionado");
    estadoActual = ESTADO_SELECCION_USUARIO;
    needsActualizarPantalla = true;
    return;
  }
  
  if (modoPartida == "") {
    Serial.println("ERROR: No hay modo seleccionado");
    estadoActual = ESTADO_SELECCION_MODO;
    needsActualizarPantalla = true;
    return;
  }
  
  Serial.print("Jugador: ");
  Serial.println(usuarios[usuarioActual].nombre);
  Serial.print("Modo: ");
  Serial.println(modoPartida);
  
  // Reiniciar variables del juego
  quizIniciado = true;
  quizCompletado = false;
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  estadoActual = ESTADO_JUGANDO;
  
  if (telegramConnected) {
    String mensaje = "Partida iniciada! Jugador: ";
    mensaje += usuarios[usuarioActual].nombre;
    mensaje += " Modo: ";
    mensaje += modoPartida;
    bot.sendMessage(CHAT_ID, mensaje);
  }
  
  mostrarPregunta();
  Serial.println("Juego iniciado correctamente");
}

void reiniciarQuiz()
{
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  quizCompletado = false;
  quizIniciado = false;
  estadoActual = ESTADO_INICIO;
  needsActualizarPantalla = true;
}

void postPuntajeToAPI(String nombre, int puntaje) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://" + String(API_HOST) + ":8000/accion/actualizar_puntaje";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"nombre\": \"" + nombre + "\", \"puntaje\": " + String(puntaje) + "}";
  http.POST(body);
  http.end();
}

// Función optimizada de actualización de pantalla
void actualizarPantallaSegunEstado() {
  // Solo actualizar si cambió el estado o la opción seleccionada
  if (estadoActual != ultimoEstado || opcionSeleccionada != ultimaOpcion || needsActualizarPantalla) {
    Serial.print("Actualizando pantalla - Estado: ");
    Serial.println(estadoActual);
    
    switch (estadoActual) {
      case ESTADO_INICIO: 
        mostrarPantallaConexiones(); 
        break;
      case ESTADO_SELECCION_MODO: 
        mostrarPantallaSeleccionModo(); 
        break;
      case ESTADO_SELECCION_USUARIO: 
        mostrarPantallaSeleccionUsuario(); 
        break;
      case ESTADO_ESPERA_INICIO: 
        mostrarPantallaEsperaInicio(); 
        break;
      case ESTADO_SELECCION_OPONENTE: 
        mostrarPantallaSeleccionOponente(); 
        break;
      case ESTADO_JUGANDO: 
        mostrarPregunta(); 
        break;
      case ESTADO_FINAL: 
        mostrarResultado(); 
        break;
      default: 
        mostrarPantallaConexiones(); 
        break;
    }
    
    ultimoEstado = estadoActual;
    ultimaOpcion = opcionSeleccionada;
  }
  needsActualizarPantalla = false;
}

// Polling-only button detection
// MEJORA la función buttonPressed:
bool buttonPressed() {
    int current = digitalRead(ENCODER_SW);
    unsigned long now = millis();
    
    // ⚡ AUMENTAR DEBOUNCE PARA WOKWI (más estable)
    if (current == LOW && lastButtonState == HIGH && (now - lastButtonPress) > 300) {
        lastButtonPress = now;
        lastButtonState = current;
        
        // Bloquear encoder temporalmente para evitar activaciones accidentales
        encoderEnabled = false;
        encoderDisableUntil = now + 500; // ⚡ Aumentar a 500ms
        
        //Serial.println("✅ Botón presionado");
        return true;
    }
    
    lastButtonState = current;
    
    // Reactivar encoder después del tiempo de bloqueo
    if (!encoderEnabled && millis() > encoderDisableUntil) {
        encoderEnabled = true;
    }
    
    return false;
}

// Detección por flanco simple para el encoder
// REEMPLAZA la función readEncoderStep con esta versión mejorada:
int readEncoderStep() {
    if (!encoderEnabled) return 0;
    
    static unsigned long lastEncoderTime = 0;
    unsigned long now = millis();
    
    // ⚡ Aumentar delay para Wokwi (más estable)
    if (now - lastEncoderTime < 200) return 0;
    
    int clkState = digitalRead(ENCODER_CLK);
    int dtState = digitalRead(ENCODER_DT);
    int step = 0;
    
    if (clkState != lastCLK) {
        lastEncoderTime = now;
        if (clkState == LOW) {
            // Determinar dirección basada en el estado de DT
            if (dtState != clkState) {
                step = 1;  // Sentido horario
            } else {
                step = -1; // Sentido antihorario
            }
        }
        lastCLK = clkState;
    }
    return step;
}

void setup()
{
  Serial.begin(115200); // Aumentar velocidad para mejor debug
  Serial.println("Iniciando sistema...");

  pinMode(LED_PIN, OUTPUT);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  lastCLK = digitalRead(ENCODER_CLK);
  lastDT = digitalRead(ENCODER_DT);
  lastButtonState = digitalRead(ENCODER_SW);

  // Inicializar display
  if(!display.begin(0x3c, true)) {
    Serial.println("Error inicializando OLED");
    while(1);
  }
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();
  
  display.setCursor(0, 0);
  display.println("Iniciando sistema...");
  display.display();

  simularCargaArchivos();
  inicializarUsuariosEjemplo();
  conectarTelegram();

  Serial.println("Sistema listo - ESPERANDO COMANDOS");
}

void loop()
{
  // ⚡ MANEJAR MQTT DE FORMA NO BLOQUEANTE
  if (mqttClient.connected()) {
    // Solo procesar MQTT si está conectado
    mqttClient.loop();
  } else {
    // ⚡ RECONEXIÓN NO BLOQUEANTE - solo intenta cada 5 segundos
    reconnectMQTT();
  }

  // ⚡ PROCESAR TELEGRAM (mantener frecuencia baja)
  if (millis() - lastTimeBotRan > 2000) { // ⚡ Aumentar a 2 segundos
    procesarComandosTelegram();
    lastTimeBotRan = millis();
  }

  // ⚡ ACTUALIZAR PANTALLA SI ES NECESARIO
  if (needsActualizarPantalla) {
    actualizarPantallaSegunEstado();
  }

  // ⚡ LEER ENCODER - ESTO ES LO MÁS IMPORTANTE
  int step = readEncoderStep();

  // ⚡ MÁQUINA DE ESTADOS PRINCIPAL - NUNCA BLOQUEANTE
  switch (estadoActual)
  {
    case ESTADO_INICIO:
      if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS) {
        lastEncoderStep = millis();
        estadoActual = ESTADO_SELECCION_MODO;
        opcionSeleccionada = 0;
        needsActualizarPantalla = true;
      }
      break;

    case ESTADO_SELECCION_MODO:
      if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS) {
        lastEncoderStep = millis();
        if (step > 0) opcionSeleccionada++;
        else opcionSeleccionada--;
        
        if (opcionSeleccionada < 0) opcionSeleccionada = 2;
        if (opcionSeleccionada > 2) opcionSeleccionada = 0;
        needsActualizarPantalla = true;
      }

      if (buttonPressed()) {
        String modos[] = {"1vs1", "ranking", "multijugador"};
        modoPartida = modos[opcionSeleccionada];
        estadoActual = ESTADO_SELECCION_USUARIO;
        opcionSeleccionada = 0;
        needsActualizarPantalla = true;
        Serial.println("Modo seleccionado: " + modoPartida);
      }
      break;

    case ESTADO_SELECCION_USUARIO:
      if (totalUsuarios > 0) {
        if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS) {
          lastEncoderStep = millis();
          if (step > 0) opcionSeleccionada++;
          else opcionSeleccionada--;
          
          if (opcionSeleccionada < 0) opcionSeleccionada = totalUsuarios - 1;
          if (opcionSeleccionada >= totalUsuarios) opcionSeleccionada = 0;
          needsActualizarPantalla = true;
        }

        if (buttonPressed()) {
          usuarioActual = opcionSeleccionada;
          estadoActual = ESTADO_ESPERA_INICIO;
          needsActualizarPantalla = true;
          Serial.print("Usuario seleccionado: ");
          Serial.println(usuarios[usuarioActual].nombre);
        }
      }
      break;

    case ESTADO_ESPERA_INICIO:
      if (totalUsuarios > 0) {
        if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS) {
          lastEncoderStep = millis();
          if (step > 0) opcionSeleccionada++;
          else opcionSeleccionada--;
          
          if (opcionSeleccionada < 0) opcionSeleccionada = totalUsuarios - 1;
          if (opcionSeleccionada >= totalUsuarios) opcionSeleccionada = 0;
          usuarioActual = opcionSeleccionada;
          needsActualizarPantalla = true;
        }

        if (buttonPressed()) {
          if (modoPartida == "1vs1") {
            estadoActual = ESTADO_SELECCION_OPONENTE;
            opcionSeleccionada = (usuarioActual == 0 && totalUsuarios>1) ? 1 : 0;
            needsActualizarPantalla = true;
          } else {
            iniciarJuego();
          }
        }
      }
      break;

    case ESTADO_SELECCION_OPONENTE:
      if (totalUsuarios > 1) {
        if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS) {
          lastEncoderStep = millis();
          if (step > 0) opcionSeleccionada++;
          else opcionSeleccionada--;
          
          if (opcionSeleccionada < 0) opcionSeleccionada = totalUsuarios - 1;
          if (opcionSeleccionada >= totalUsuarios) opcionSeleccionada = 0;
          
          if (opcionSeleccionada == usuarioActual) {
            opcionSeleccionada = (opcionSeleccionada + 1) % totalUsuarios;
          }
          needsActualizarPantalla = true;
        }

        if (buttonPressed()) {
          oponenteActual = opcionSeleccionada;
          iniciarJuego();
        }
      }
      break;

    case ESTADO_JUGANDO:
      if (step != 0 && millis() - lastEncoderStep > ENCODER_STEP_MS) {
        lastEncoderStep = millis();
        if (step > 0) opcionSeleccionada++;
        else opcionSeleccionada--;
        
        if (opcionSeleccionada < 0) opcionSeleccionada = 2;
        if (opcionSeleccionada > 2) opcionSeleccionada = 0;
        needsActualizarPantalla = true;
      }

      if (buttonPressed()) {
        verificarRespuesta();
      }
      break;

    case ESTADO_FINAL:
      if (buttonPressed() && millis() - lastButtonPress > 500) {
        reiniciarQuiz();
      }
      break;
  }

  // ⚡ PEQUEÑO DELAY PARA DAR RESPIRO AL SISTEMA (no bloqueante)
  delay(10);
}