#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

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

// Telegram
const String BOT_TOKEN = "8238234652:AAEVwkqELgLiu8f_RpWsZlKfxq9azuSubUI";
const String CHAT_ID = "2044158296";

// Clients
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
bool quizCompletado = false;
bool quizIniciado = false;

// Variables encoder/button
volatile int encoderValue = 0;
volatile bool buttonPressed = false;
unsigned long lastButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 200;

// Flag para actualización de pantalla
volatile bool needsActualizarPantalla = true;

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
  ESTADO_SELECCION_USUARIO,
  ESTADO_JUGANDO,
  ESTADO_MOSTRANDO_RESPUESTA,
  ESTADO_FINAL
};

EstadoSistema estadoActual = ESTADO_INICIO;

// ==================== PROTOTIPOS DE FUNCIONES ====================
void IRAM_ATTR handleEncoder();
void IRAM_ATTR handleButton();
void mostrarPantallaInicio();
void mostrarPantallaSeleccionUsuario();
void mostrarPregunta();
void simularCargaArchivos();
void inicializarUsuariosEjemplo();
int buscarUsuario(String nombre);
void agregarUsuario(String nombre);
void actualizarPuntuacionUsuario();
void mostrarResultado();
void verificarRespuesta();
void reiniciarQuiz();
void actualizarPantallaSegunEstado();
void iniciarJuego();
void conectarTelegram();
void procesarComandosTelegram();
bool checkMqttConnection();
void callbackMQTT(char* topic, byte* payload, unsigned int length);

// ==================== INTERRUPCIONES ENCODER ====================
void IRAM_ATTR handleEncoder() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  if (interruptTime - lastInterruptTime > 5) {
    int clkState = digitalRead(ENCODER_CLK);
    int dtState = digitalRead(ENCODER_DT);
    
    if (clkState == dtState) {
      encoderValue++;
    } else {
      encoderValue--;
    }
    needsActualizarPantalla = true;
  }
  lastInterruptTime = interruptTime;
}

void IRAM_ATTR handleButton() {
  static unsigned long lastButtonInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  if (interruptTime - lastButtonInterruptTime > BUTTON_DEBOUNCE_MS) {
    buttonPressed = true;
    needsActualizarPantalla = true;
  }
  lastButtonInterruptTime = interruptTime;
}

// ==================== FUNCIONES MQTT ====================
void callbackMQTT(char *topic, byte *payload, unsigned int length) {
  String mensaje = "";
  for (int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }
  mensaje.trim();
  
  Serial.print("MQTT Recibido: ");
  Serial.println(mensaje);

  if (mensaje == "iniciar_partida") {
    if (usuarioActual >= 0) {
      iniciarJuego();
    } else {
      estadoActual = ESTADO_SELECCION_USUARIO;
      needsActualizarPantalla = true;
    }
  } else if (mensaje.startsWith("usuario:")) {
    String nombre = mensaje.substring(8);
    nombre.trim(); 
    nombre.toUpperCase();
    if (buscarUsuario(nombre) == -1) {
      agregarUsuario(nombre);
    }
  }
}

bool checkMqttConnection() {
  if (mqttClient.connected()) {
    return true;
  }
  
  unsigned long now = millis();
  if (now - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
    lastMqttReconnectAttempt = now;
    
    Serial.print("Conectando MQTT... ");
    String clientId = "ESP32Quiz-" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("conectado!");
      mqttClient.subscribe("wokwi/acciones");
      return true;
    } else {
      Serial.print("falló, rc=");
      Serial.println(mqttClient.state());
      return false;
    }
  }
  return false;
}

// ==================== FUNCIONES TELEGRAM ====================
void conectarTelegram() {
  Serial.println("Conectando a WiFi...");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Conectando WiFi...");
  display.display();

  WiFi.begin(ssid, password);

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(1000);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado a WiFi!");
    
    // Configurar MQTT
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(callbackMQTT);

    secureClient.setInsecure();

    // Intentar conectar Telegram
    int botUsuarios = bot.getUpdates(0);
    if (botUsuarios != -1) {
      telegramConnected = true;
      Serial.println("Conexion con Telegram exitosa!");
      bot.sendMessage(CHAT_ID, "Quiz ESP32 conectado y listo!", "");
    } else {
      Serial.println("Telegram no disponible, continuando...");
    }
    
    estadoActual = ESTADO_INICIO;
    needsActualizarPantalla = true;
    
  } else {
    Serial.println("Error conectando a WiFi");
    estadoActual = ESTADO_INICIO;
    needsActualizarPantalla = true;
  }
}

void procesarComandosTelegram() {
  if (!telegramConnected) return;

  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  if (numNewMessages) {
    Serial.print("Mensajes Telegram: ");
    Serial.println(numNewMessages);
  }

  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;
      text.trim();
      
      if (text == "/start" || text == "/help") {
        String welcome = "Quiz ESP32 - Comandos:\n";
        welcome += "/ingresar_usuario NOMBRE\n";
        welcome += "/iniciar_partida\n";
        welcome += "/estado\n";
        bot.sendMessage(chat_id, welcome);
      }
      else if (text == "/estado") {
        String estado_msg = "Estado: ";
        estado_msg += (usuarioActual >= 0 ? usuarios[usuarioActual].nombre : "Ninguno");
        estado_msg += "\nPregunta: ";
        estado_msg += (preguntaActual + 1);
        estado_msg += "/";
        estado_msg += totalPreguntas;
        bot.sendMessage(chat_id, estado_msg);
      }
      else if (text.startsWith("/ingresar_usuario")) {
        String nombre = text.substring(17);
        nombre.trim();
        nombre.toUpperCase();
        if (nombre.length() > 0) {
          if (buscarUsuario(nombre) == -1) {
            agregarUsuario(nombre);
            bot.sendMessage(chat_id, "Usuario " + nombre + " agregado!");
          } else {
            bot.sendMessage(chat_id, "Usuario " + nombre + " ya existe!");
          }
        }
      }
      else if (text == "/iniciar_partida") {
        if (usuarioActual >= 0) {
          iniciarJuego();
          bot.sendMessage(chat_id, "Partida iniciada con " + usuarios[usuarioActual].nombre);
        } else {
          bot.sendMessage(chat_id, "Primero selecciona un usuario!");
        }
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ==================== FUNCIONES DE PANTALLA ====================
void mostrarPantallaInicio() {
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
  display.println("Gira encoder para");
  display.setCursor(0, 48);
  display.println("seleccionar usuario");
  display.setCursor(0, 56);
  display.println("o usa Telegram");
  display.display();
}

void mostrarPantallaSeleccionUsuario() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SELECCIONA USUARIO:");

  if (totalUsuarios == 0) {
    display.setCursor(5, 20);
    display.println("No hay usuarios");
    display.setCursor(5, 32);
    display.println("Usa Telegram:");
    display.setCursor(5, 44);
    display.println("/ingresar_usuario");
    display.setCursor(5, 56);
    display.println("para crear uno");
  } else {
    int inicio = 0;
    int usuariosPorPantalla = 4;

    if (totalUsuarios > usuariosPorPantalla) {
      inicio = (opcionSeleccionada / usuariosPorPantalla) * usuariosPorPantalla;
    }

    for (int i = 0; i < usuariosPorPantalla && (inicio + i) < totalUsuarios; i++) {
      int indiceUsuario = inicio + i;
      display.setCursor(5, 15 + i * 12);
      if (indiceUsuario == opcionSeleccionada) {
        display.print("> ");
      } else {
        display.print("  ");
      }
      display.println(usuarios[indiceUsuario].nombre);
    }

    if (totalUsuarios > usuariosPorPantalla) {
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

void mostrarPregunta() {
    display.clearDisplay();

    // Header con información
    display.setCursor(0, 0);
    if (usuarioActual >= 0) {
        String nombreDisplay = usuarios[usuarioActual].nombre;
        if (nombreDisplay.length() > 8) {
            nombreDisplay = nombreDisplay.substring(0, 8) + ".";
        }
        display.print(nombreDisplay);
    }
    
    display.setCursor(105, 0);
    display.print(preguntaActual + 1);
    display.print("/");
    display.print(totalPreguntas);
    
    // Mostrar pregunta
    String preguntaTexto = preguntas[preguntaActual].texto;
    preguntaTexto.replace("¿", "?");
    preguntaTexto.replace("¡", "!");
    
    int yPos = 12;
    int maxWidth = 120;
    int charWidth = 6;
    
    String lineaActual = "";
    for (int i = 0; i < preguntaTexto.length(); i++) {
        char c = preguntaTexto[i];
        lineaActual += c;
        
        if (lineaActual.length() * charWidth >= maxWidth || (c == ' ' && lineaActual.length() * charWidth >= maxWidth - 20)) {
            display.setCursor(0, yPos);
            display.println(lineaActual);
            yPos += 10;
            lineaActual = "";
        }
    }
    
    if (lineaActual.length() > 0) {
        display.setCursor(0, yPos);
        display.println(lineaActual);
        yPos += 12;
    }
    
    // Mostrar opciones
    for (int i = 0; i < 3; i++) {
        display.setCursor(5, yPos + (i * 10));
        if (i == opcionSeleccionada) {
            display.print(">");
        } else {
            display.print(" ");
        }
        display.print(" ");
        
        String opcion = preguntas[preguntaActual].opciones[i];
        if (opcion.length() > 18) {
            opcion = opcion.substring(0, 18) + ".";
        }
        display.println(opcion);
    }

    display.display();
}

void mostrarResultado() {
  display.clearDisplay();
  display.setCursor(20, 10);
  display.println("QUIZ COMPLETADO!");
  display.setCursor(10, 25);
  if (usuarioActual >= 0) {
    display.print("Jugador: ");
    display.println(usuarios[usuarioActual].nombre);
  }
  display.setCursor(30, 40);
  display.print("Puntos: ");
  display.println(puntuacionTotal);
  display.setCursor(5, 55);
  display.println("Click para reiniciar");
  display.display();
}

// ==================== FUNCIONES DEL QUIZ ====================
void simularCargaArchivos() {
  for (int i = 0; i < totalPreguntas; i++) {
    String linea = ARCHIVO_PREGUNTAS[i];

    int separadores[5];
    int sepIndex = 0;

    for (int j = 0; j < linea.length() && sepIndex < 5; j++) {
      if (linea.charAt(j) == ';') {
        separadores[sepIndex++] = j;
      }
    }

    if (sepIndex == 4) {
      preguntas[i].texto = linea.substring(0, separadores[0]);
      preguntas[i].opciones[0] = linea.substring(separadores[0] + 1, separadores[1]);
      preguntas[i].opciones[1] = linea.substring(separadores[1] + 1, separadores[2]);
      preguntas[i].opciones[2] = linea.substring(separadores[2] + 1, separadores[3]);
      preguntas[i].respuestaCorrecta = linea.substring(separadores[3] + 1).toInt();
      preguntas[i].puntaje = ARCHIVO_PUNTAJES[i];
    }
  }
}

void inicializarUsuariosEjemplo() {
  totalUsuarios = 0;
  // Agregar algunos usuarios por defecto
  agregarUsuario("JUGADOR1");
  agregarUsuario("JUGADOR2");
}

int buscarUsuario(String nombre) {
  for(int i = 0; i < totalUsuarios; i++) {
    if(usuarios[i].nombre == nombre) {
      return i;
    }
  }
  return -1;
}

void agregarUsuario(String nombre) {
  if (totalUsuarios < 20) {
    usuarios[totalUsuarios].nombre = nombre;
    usuarios[totalUsuarios].puntuacionMaxima = 0;
    usuarios[totalUsuarios].partidasJugadas = 0;
    totalUsuarios++;
    
    Serial.print("Usuario agregado: ");
    Serial.println(nombre);
    
    needsActualizarPantalla = true;
  }
}

void actualizarPuntuacionUsuario() {
  if (usuarioActual >= 0) {
    usuarios[usuarioActual].partidasJugadas++;
    if (puntuacionTotal > usuarios[usuarioActual].puntuacionMaxima) {
      usuarios[usuarioActual].puntuacionMaxima = puntuacionTotal;
    }
  }
}

void verificarRespuesta() {
  bool correcta = (opcionSeleccionada == preguntas[preguntaActual].respuestaCorrecta);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(preguntas[preguntaActual].texto);
  display.setCursor(0, 15);
  display.print("Elegiste: ");
  display.println(preguntas[preguntaActual].opciones[opcionSeleccionada]);

  if (correcta) {
    puntuacionTotal += preguntas[preguntaActual].puntaje;
    display.setCursor(0, 30);
    display.print("CORRECTO! +");
    display.print(preguntas[preguntaActual].puntaje);
    digitalWrite(LED_PIN, HIGH);
  } else {
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
  if (preguntaActual >= totalPreguntas) {
    quizCompletado = true;
    actualizarPuntuacionUsuario();
    estadoActual = ESTADO_FINAL;
    mostrarResultado();
    
    if (telegramConnected && usuarioActual >= 0) {
      String mensaje = "Partida completada! ";
      mensaje += usuarios[usuarioActual].nombre;
      mensaje += " - Puntos: ";
      mensaje += puntuacionTotal;
      bot.sendMessage(CHAT_ID, mensaje);
    }
  } else {
    opcionSeleccionada = 0;
    estadoActual = ESTADO_JUGANDO;
    mostrarPregunta();
  }
}

void iniciarJuego() {
  quizIniciado = true;
  quizCompletado = false;
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  estadoActual = ESTADO_JUGANDO;
  mostrarPregunta();
}

void reiniciarQuiz() {
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  quizCompletado = false;
  quizIniciado = false;
  estadoActual = ESTADO_SELECCION_USUARIO;
  usuarioActual = -1;
  needsActualizarPantalla = true;
}

void actualizarPantallaSegunEstado() {
  if (needsActualizarPantalla) {
    switch (estadoActual) {
      case ESTADO_INICIO: 
        mostrarPantallaInicio(); 
        break;
      case ESTADO_SELECCION_USUARIO: 
        mostrarPantallaSeleccionUsuario(); 
        break;
      case ESTADO_JUGANDO: 
        mostrarPregunta(); 
        break;
      case ESTADO_FINAL: 
        mostrarResultado(); 
        break;
    }
    needsActualizarPantalla = false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando sistema...");

  pinMode(LED_PIN, OUTPUT);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  // Configurar interrupciones
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_SW), handleButton, FALLING);

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

  Serial.println("Sistema listo");
}

void loop() {
  // Procesar movimiento del encoder
  static int lastEncoderValue = 0;
  if (encoderValue != lastEncoderValue) {
    int diff = encoderValue - lastEncoderValue;
    lastEncoderValue = encoderValue;
    
    if (estadoActual == ESTADO_INICIO) {
      estadoActual = ESTADO_SELECCION_USUARIO;
      opcionSeleccionada = 0;
    } else if (estadoActual == ESTADO_SELECCION_USUARIO) {
      if (diff > 0) opcionSeleccionada++;
      else opcionSeleccionada--;
      
      if (opcionSeleccionada < 0) opcionSeleccionada = totalUsuarios - 1;
      if (opcionSeleccionada >= totalUsuarios) opcionSeleccionada = 0;
    } else if (estadoActual == ESTADO_JUGANDO) {
      if (diff > 0) opcionSeleccionada++;
      else opcionSeleccionada--;
      
      if (opcionSeleccionada < 0) opcionSeleccionada = 2;
      if (opcionSeleccionada > 2) opcionSeleccionada = 0;
    }
    needsActualizarPantalla = true;
  }

  // Procesar botón presionado
  if (buttonPressed) {
    buttonPressed = false;
    
    if (estadoActual == ESTADO_SELECCION_USUARIO && totalUsuarios > 0) {
      usuarioActual = opcionSeleccionada;
      iniciarJuego();
    } else if (estadoActual == ESTADO_JUGANDO) {
      verificarRespuesta();
    } else if (estadoActual == ESTADO_FINAL) {
      reiniciarQuiz();
    }
  }

  // Procesar Telegram
  if (millis() - lastTimeBotRan > 1000) {
    procesarComandosTelegram();
    lastTimeBotRan = millis();
  }

  // Manejar MQTT
  if (!mqttClient.connected()) {
    checkMqttConnection();
  } else {
    mqttClient.loop();
  }

  // Actualizar pantalla si es necesario
  actualizarPantallaSegunEstado();
}