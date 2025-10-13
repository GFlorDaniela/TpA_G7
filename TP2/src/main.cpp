#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <PubSubClient.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Pines del Encoder Físico
#define ENCODER_CLK 18
#define ENCODER_DT 5  
#define ENCODER_SW 19
#define LED_PIN 23

// WiFi y Telegram
const char *ssid = "ACNET2";
const char *password = "";
const String BOT_TOKEN = "8238234652:AAEVwkqELgLiu8f_RpWsZlKfxq9azuSubUI";
const String CHAT_ID = "2044158296";

// Clients
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);
WiFiClient clientMQTT;

// --- Configuración de MQTT ---
const char* mqtt_server = "test.mosquitto.org"; 
const int mqtt_port = 1883;
const char* mqtt_client_id = "esp32-file-sync"; // Un nombre único para la placa ESP32
PubSubClient client(clientMQTT);

// Estructuras de datos
struct Pregunta {
  String texto;
  String opciones[3];
  int respuestaCorrecta;
  int puntaje;
};

struct Usuario {
  String nombre;
  int puntuacionMaxima;
  int partidasJugadas;
};

// Variables globales
Pregunta preguntas[50];
Usuario usuarios[100];
int totalPreguntas = 0;
int totalUsuarios = 0;
int preguntaActual = 0;
int opcionSeleccionada = 0;
int puntuacionTotal = 0;
int usuarioActual = -1;
bool quizCompletado = false;
bool quizIniciado = false;

// Variables para Encoder por Interrupciones
volatile int encoderPos = 0;
volatile bool encoderChanged = false;
volatile unsigned long lastEncoderInterrupt = 0;
const unsigned long DEBOUNCE_DELAY = 1000;

// Variables para Botón
volatile bool buttonPressed = false;
volatile unsigned long lastButtonInterrupt = 0;
const unsigned long BUTTON_DEBOUNCE = 200;

// Estados del sistema
enum EstadoSistema {
  ESTADO_INICIO,
  ESTADO_SELECCION_USUARIO,
  ESTADO_JUGANDO,
  ESTADO_FINAL
};

EstadoSistema estadoActual = ESTADO_INICIO;
bool telegramConnected = false;
unsigned long lastTimeBotRan = 0;

// ==================== DECLARACIONES DE FUNCIONES ====================

// Funciones SPIFFS
bool inicializarSPIFFS();
bool cargarPreguntas();
bool guardarUsuarios();
bool cargarUsuarios();
bool guardarPreguntasEnSPIFFS();
void sincronizarArchivo(const char* rutaArchivo, const char* topico);
void reconnect();

// Funciones Telegram
void conectarTelegram();
void procesarComandosTelegram();

// Funciones Usuarios
int buscarUsuario(String nombre);
void agregarUsuario(String nombre);
void ordenarUsuariosPorRanking();

// Funciones Pantalla
void mostrarPantallaInicio();
void mostrarPantallaSeleccionUsuario();
void mostrarPregunta();
void mostrarResultado();
void actualizarPantalla();

// Funciones Quiz
void verificarRespuesta();
void iniciarJuego();
void reiniciarQuiz();

// Funciones Encoder
void IRAM_ATTR encoderISR();
void IRAM_ATTR buttonISR();
void procesarEncoder();
void procesarBoton();

// ==================== INTERRUPCIONES DEL ENCODER ====================

void IRAM_ATTR encoderISR() {
  unsigned long currentTime = micros();
  
  if (currentTime - lastEncoderInterrupt < DEBOUNCE_DELAY) {
    return;
  }
  
  lastEncoderInterrupt = currentTime;
  
  int clkState = digitalRead(ENCODER_CLK);
  int dtState = digitalRead(ENCODER_DT);
  
  if (clkState == dtState) {
    encoderPos++;
  } else {
    encoderPos--;
  }
  
  encoderChanged = true;
}

void IRAM_ATTR buttonISR() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastButtonInterrupt < BUTTON_DEBOUNCE) {
    return;
  }
  
  lastButtonInterrupt = currentTime;
  buttonPressed = true;
}

// ==================== MANEJO DEL ENCODER ====================

void procesarEncoder() {
  if (!encoderChanged) return;
  
  noInterrupts();
  int cambio = encoderPos;
  encoderPos = 0;
  encoderChanged = false;
  interrupts();
  
  if (cambio != 0) {
    switch(estadoActual) {
      case ESTADO_SELECCION_USUARIO:
        if (totalUsuarios > 0) {
          if (cambio > 0) {
            opcionSeleccionada = (opcionSeleccionada + 1) % totalUsuarios;
          } else {
            opcionSeleccionada = (opcionSeleccionada - 1 + totalUsuarios) % totalUsuarios;
          }
        }
        break;
        
      case ESTADO_JUGANDO:
        if (cambio > 0) {
          opcionSeleccionada = (opcionSeleccionada + 1) % 3;
        } else {
          opcionSeleccionada = (opcionSeleccionada - 1 + 3) % 3;
        }
        break;
    }
    
    actualizarPantalla();
    Serial.println("Encoder: " + String(cambio) + " - Opción: " + String(opcionSeleccionada));
  }
}

void procesarBoton() {
  if (!buttonPressed) return;
  
  noInterrupts();
  buttonPressed = false;
  interrupts();
  
  Serial.println("Botón presionado");
  
  switch(estadoActual) {
    case ESTADO_INICIO:
      estadoActual = ESTADO_SELECCION_USUARIO;
      opcionSeleccionada = 0;
      break;
    
    case ESTADO_SELECCION_USUARIO:
      if (totalUsuarios > 0) {
        usuarioActual = opcionSeleccionada;
        iniciarJuego();
      }
      break;
    
    case ESTADO_JUGANDO:
      verificarRespuesta();
      break;
    
    case ESTADO_FINAL:
      reiniciarQuiz();
      break;
  }
  
  actualizarPantalla();
}

// ==================== FUNCIONES SPIFFS ====================

bool inicializarSPIFFS() {
  if(!SPIFFS.begin(true)){
    Serial.println("Error al montar SPIFFS");
    return false;
  }
  Serial.println("SPIFFS montado correctamente");
  return true;
}

void sincronizarArchivo(const char* rutaArchivo, const char* topico) {
    // 1. Abrir el archivo para lectura
    File file = SPIFFS.open(rutaArchivo, "r");
    if (!file) {
        Serial.println("Error al abrir el archivo para leerlo.");
        return;
    }

    // 2. Leer todo el contenido del archivo
    String contenidoArchivo = file.readString();
    file.close();

    // 3. Publicar el contenido en el tópico MQTT
    if (client.connected()) {
        client.publish(topico, contenidoArchivo.c_str());
        Serial.print("Archivo ");
        Serial.print(rutaArchivo);
        Serial.print(" sincronizado en el tópico ");
        Serial.println(topico);
    } else {
        Serial.println("Cliente MQTT no conectado. No se pudo sincronizar.");
    }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (client.connect(mqtt_client_id)) {
      Serial.println("¡Conectado!");
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" intentando de nuevo en 5 segundos");
      delay(5000);
    }
  }
}

bool cargarPreguntas() {
  File archivo = SPIFFS.open("/preguntas.json", "r");
  if(!archivo){
    Serial.println("Error al abrir preguntas.json");
    return false;
  }
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, archivo);
  archivo.close();
  
  if(error){
    Serial.println("Error al parsear JSON: " + String(error.c_str()));
    return false;
  }
  
  JsonArray preguntasArray = doc.as<JsonArray>();
  totalPreguntas = 0;
  
  for(JsonObject preguntaObj : preguntasArray) {
    if(totalPreguntas >= 50) break;
    
    preguntas[totalPreguntas].texto = preguntaObj["texto"].as<String>();
    preguntas[totalPreguntas].opciones[0] = preguntaObj["opciones"][0].as<String>();
    preguntas[totalPreguntas].opciones[1] = preguntaObj["opciones"][1].as<String>();
    preguntas[totalPreguntas].opciones[2] = preguntaObj["opciones"][2].as<String>();
    preguntas[totalPreguntas].respuestaCorrecta = preguntaObj["correcta"].as<int>();
    preguntas[totalPreguntas].puntaje = preguntaObj["puntaje"].as<int>();
    
    totalPreguntas++;
  }
  
  Serial.println("Preguntas cargadas: " + String(totalPreguntas));
  
  // Ordenar usuarios al cargar
  ordenarUsuariosPorRanking();
  return true;
}

bool guardarPreguntasEnSPIFFS() {
  File archivo = SPIFFS.open("/preguntas.json", "w");
  if(!archivo){
    Serial.println("Error al crear preguntas.json");
    return false;
  }
  
  DynamicJsonDocument doc(8192);
  JsonArray preguntasArray = doc.to<JsonArray>();
  
  for(int i = 0; i < totalPreguntas; i++) {
    JsonObject preguntaObj = preguntasArray.createNestedObject();
    preguntaObj["texto"] = preguntas[i].texto;
    
    JsonArray opcionesArray = preguntaObj.createNestedArray("opciones");
    opcionesArray.add(preguntas[i].opciones[0]);
    opcionesArray.add(preguntas[i].opciones[1]);
    opcionesArray.add(preguntas[i].opciones[2]);
    
    preguntaObj["correcta"] = preguntas[i].respuestaCorrecta;
    preguntaObj["puntaje"] = preguntas[i].puntaje;
  }
  
  if(serializeJsonPretty(doc, archivo) == 0){
    Serial.println("Error al escribir en preguntas.json");
    archivo.close();
    return false;
  }
  
  archivo.close();
  sincronizarArchivo("/preguntas.json", "esp32/archivos/preguntas.json");
  Serial.println("Preguntas guardadas correctamente");
  return true;
}

bool guardarUsuarios() {
  File archivo = SPIFFS.open("/usuarios.json", "w");
  if(!archivo){
    Serial.println("Error al crear usuarios.json");
    return false;
  }
  
  DynamicJsonDocument doc(8192);
  JsonArray usuariosArray = doc.to<JsonArray>();
  
  for(int i = 0; i < totalUsuarios; i++) {
    JsonObject usuarioObj = usuariosArray.createNestedObject();
    usuarioObj["nombre"] = usuarios[i].nombre;
    usuarioObj["puntuacionMaxima"] = usuarios[i].puntuacionMaxima;
    usuarioObj["partidasJugadas"] = usuarios[i].partidasJugadas;
  }
  
  if(serializeJsonPretty(doc, archivo) == 0){
    Serial.println("Error al escribir en usuarios.json");
    archivo.close();
    return false;
  }
  
  archivo.close();
  sincronizarArchivo("/usuarios.json", "esp32/archivos/usuarios.json");
  return true;
}

bool cargarUsuarios() {
  if(!SPIFFS.exists("/usuarios.json")) {
    Serial.println("Archivo usuarios.json no existe, se creará uno nuevo");
    return guardarUsuarios();
  }
  
  File archivo = SPIFFS.open("/usuarios.json", "r");
  if(!archivo){
    Serial.println("Error al abrir usuarios.json");
    return false;
  }
  
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, archivo);
  archivo.close();
  
  if(error){
    Serial.println("Error al parsear JSON: " + String(error.c_str()));
    return false;
  }
  
  JsonArray usuariosArray = doc.as<JsonArray>();
  totalUsuarios = 0;
  
  for(JsonObject usuarioObj : usuariosArray) {
    if(totalUsuarios >= 100) break;
    
    usuarios[totalUsuarios].nombre = usuarioObj["nombre"].as<String>();
    usuarios[totalUsuarios].puntuacionMaxima = usuarioObj["puntuacionMaxima"].as<int>();
    usuarios[totalUsuarios].partidasJugadas = usuarioObj["partidasJugadas"].as<int>();
    
    totalUsuarios++;
  }
  
  Serial.println("Usuarios cargados: " + String(totalUsuarios));
  return true;
}

// ==================== FUNCIONES DE USUARIOS ====================

int buscarUsuario(String nombre) {
  for(int i = 0; i < totalUsuarios; i++) {
    if(usuarios[i].nombre == nombre) {
      return i;
    }
  }
  return -1;
}

void agregarUsuario(String nombre) {
  if(totalUsuarios < 100) {
    usuarios[totalUsuarios].nombre = nombre;
    usuarios[totalUsuarios].puntuacionMaxima = 0;
    usuarios[totalUsuarios].partidasJugadas = 0;
    totalUsuarios++;
    guardarUsuarios();
    Serial.println("Usuario creado: " + nombre);
  }
}

// Función para ordenar usuarios por ranking
void ordenarUsuariosPorRanking() {
  for (int i = 0; i < totalUsuarios - 1; i++) {
    for (int j = i + 1; j < totalUsuarios; j++) {
      // Primero por puntuación máxima (descendente)
      if (usuarios[j].puntuacionMaxima > usuarios[i].puntuacionMaxima) {
        Usuario temp = usuarios[i];
        usuarios[i] = usuarios[j];
        usuarios[j] = temp;
      }
      // Si misma puntuación, por partidas jugadas (descendente)
      else if (usuarios[j].puntuacionMaxima == usuarios[i].puntuacionMaxima) {
        if (usuarios[j].partidasJugadas > usuarios[i].partidasJugadas) {
          Usuario temp = usuarios[i];
          usuarios[i] = usuarios[j];
          usuarios[j] = temp;
        }
        // Si mismo número de partidas, por orden alfabético
        else if (usuarios[j].partidasJugadas == usuarios[i].partidasJugadas) {
          if (usuarios[j].nombre.compareTo(usuarios[i].nombre) < 0) {
            Usuario temp = usuarios[i];
            usuarios[i] = usuarios[j];
            usuarios[j] = temp;
          }
        }
      }
    }
  }
}

// ==================== FUNCIONES TELEGRAM ====================

void conectarTelegram() {
  Serial.println("Conectando a WiFi...");
  
  WiFi.begin(ssid, password);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(1000);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado a WiFi!");
    
    // Configurar hora NTP
    configTime(0, 0, "pool.ntp.org");
    time_t now = time(nullptr);
    int timeout = 0;
    while (now < 24 * 3600 && timeout < 30) {
      delay(500);
      now = time(nullptr);
      timeout++;
    }

    secureClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);

    int botUsuarios = bot.getUpdates(0);
    if (botUsuarios != -1) {
      telegramConnected = true;
      Serial.println("Conexión con Telegram exitosa!");
      bot.sendMessage(CHAT_ID, "🤖 Quiz ESP32 conectado! Usa /help para ver comandos", "");
    }
  }
}

void procesarComandosTelegram() {
  if (!telegramConnected) return;

  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  
  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;
      text.trim();
      
      if (text.startsWith("/ingresar_usuario")) {
        String nombre = text.substring(String("/ingresar_usuario").length());
        nombre.trim();
        nombre.toUpperCase();
        
        if (nombre.length() > 0) {
          int usuarioExistente = buscarUsuario(nombre);
          if (usuarioExistente == -1) {
            agregarUsuario(nombre);
            bot.sendMessage(chat_id, "✅ Usuario creado: " + nombre, "");
          } else {
            bot.sendMessage(chat_id, "⚠️ Usuario ya existe: " + nombre, "");
          }
        } else {
          bot.sendMessage(chat_id, "❌ Ingresa un nombre: /ingresar_usuario NOMBRE", "");
        }
      }
      else if (text == "/ranking") {
        ordenarUsuariosPorRanking();
        String rankingMsg = "🏆 *RANKING* 🏆\n\n";
        if (totalUsuarios == 0) {
          rankingMsg += "No hay usuarios registrados";
        } else {
          for (int k = 0; k < totalUsuarios; k++) {
            rankingMsg += String(k + 1) + ". " + usuarios[k].nombre + 
                         " - " + String(usuarios[k].puntuacionMaxima) + " pts" +
                         " (" + String(usuarios[k].partidasJugadas) + " partidas)\n";
          }
        }
        bot.sendMessage(chat_id, rankingMsg, "");
      }
      else if (text == "/usuarios") {
        String usuariosMsg = "👥 *USUARIOS REGISTRADOS* 👥\n\n";
        if (totalUsuarios == 0) {
          usuariosMsg += "No hay usuarios registrados";
        } else {
          for (int k = 0; k < totalUsuarios; k++) {
            usuariosMsg += "• " + usuarios[k].nombre + "\n";
          }
          usuariosMsg += "\nTotal: " + String(totalUsuarios) + " usuarios";
        }
        bot.sendMessage(chat_id, usuariosMsg, "");
      }
      else if (text == "/estadisticas") {
        String statsMsg = "📊 *ESTADÍSTICAS POR USUARIO* 📊\n\n";
        if (totalUsuarios == 0) {
          statsMsg += "No hay usuarios registrados";
        } else {
          for (int k = 0; k < totalUsuarios; k++) {
            statsMsg += "👤 " + usuarios[k].nombre + ":\n";
            statsMsg += "   Puntuación máxima: " + String(usuarios[k].puntuacionMaxima) + " pts\n";
            statsMsg += "   Partidas jugadas: " + String(usuarios[k].partidasJugadas) + "\n\n";
          }
        }
        bot.sendMessage(chat_id, statsMsg, "");
      }
      else if (text == "/preguntas") {
        String preguntasMsg = "❓ *PREGUNTAS DISPONIBLES* ❓\n\n";
        if (totalPreguntas == 0) {
          preguntasMsg += "No hay preguntas cargadas";
        } else {
          for (int k = 0; k < totalPreguntas; k++) {
            preguntasMsg += String(k + 1) + ". " + preguntas[k].texto + "\n";
            preguntasMsg += "   Opciones: " + preguntas[k].opciones[0] + ", " + 
                           preguntas[k].opciones[1] + ", " + preguntas[k].opciones[2] + "\n";
            preguntasMsg += "   Correcta: " + String(preguntas[k].respuestaCorrecta + 1) + 
                           " - Puntos: " + String(preguntas[k].puntaje) + "\n\n";
          }
          preguntasMsg += "Total: " + String(totalPreguntas) + " preguntas";
        }
        bot.sendMessage(chat_id, preguntasMsg, "");
      }
      else if (text.startsWith("/agregar_pregunta")) {
        // Formato: /agregar_pregunta Texto|Op1|Op2|Op3|Correcta|Puntos
        String parametros = text.substring(String("/agregar_pregunta").length());
        parametros.trim();
        
        if (parametros.length() == 0) {
          String ayuda = "📝 *AGREGAR PREGUNTA* 📝\n\n";
          ayuda += "Formato:\n";
          ayuda += "/agregar_pregunta Texto|Opción1|Opción2|Opción3|Correcta|Puntos\n\n";
          ayuda += "Ejemplo:\n";
          ayuda += "/agregar_pregunta Que lenguaje usa Arduino|C++|Python|Java|1|10\n\n";
          ayuda += "• Correcta: 1, 2 o 3 (número de opción)\n";
          ayuda += "• Puntos: valor numérico";
          bot.sendMessage(chat_id, ayuda, "");
        } else {
          if (totalPreguntas >= 50) {
            bot.sendMessage(chat_id, "❌ Límite máximo de 50 preguntas alcanzado", "");
          } else {
            // Parsear parámetros
            int separadores[5];
            int sepCount = 0;
            
            for (int i = 0; i < parametros.length() && sepCount < 5; i++) {
              if (parametros.charAt(i) == '|') {
                separadores[sepCount] = i;
                sepCount++;
              }
            }
            
            if (sepCount != 5) {
              bot.sendMessage(chat_id, "❌ Formato incorrecto. Usa: Texto|Op1|Op2|Op3|Correcta|Puntos", "");
            } else {
              // Extraer partes
              String texto = parametros.substring(0, separadores[0]);
              String op1 = parametros.substring(separadores[0] + 1, separadores[1]);
              String op2 = parametros.substring(separadores[1] + 1, separadores[2]);
              String op3 = parametros.substring(separadores[2] + 1, separadores[3]);
              int correcta = parametros.substring(separadores[3] + 1, separadores[4]).toInt() - 1;
              int puntos = parametros.substring(separadores[4] + 1).toInt();
              
              // Validaciones
              if (texto.length() == 0 || op1.length() == 0 || op2.length() == 0 || op3.length() == 0) {
                bot.sendMessage(chat_id, "❌ Todos los campos deben tener contenido", "");
              } else if (correcta < 0 || correcta > 2) {
                bot.sendMessage(chat_id, "❌ Opción correcta debe ser 1, 2 o 3", "");
              } else if (puntos <= 0) {
                bot.sendMessage(chat_id, "❌ Puntos debe ser mayor a 0", "");
              } else if (texto.length() > 100) {
                bot.sendMessage(chat_id, "❌ El texto es muy largo (máx 100 caracteres)", "");
              } else {
                // Agregar pregunta
                preguntas[totalPreguntas].texto = texto;
                preguntas[totalPreguntas].opciones[0] = op1;
                preguntas[totalPreguntas].opciones[1] = op2;
                preguntas[totalPreguntas].opciones[2] = op3;
                preguntas[totalPreguntas].respuestaCorrecta = correcta;
                preguntas[totalPreguntas].puntaje = puntos;
                totalPreguntas++;
                
                // Guardar en SPIFFS
                guardarPreguntasEnSPIFFS();
                
                String confirmacion = "✅ *PREGUNTA AGREGADA* ✅\n\n";
                confirmacion += "Texto: " + texto + "\n";
                confirmacion += "Opciones: " + op1 + ", " + op2 + ", " + op3 + "\n";
                confirmacion += "Correcta: Opción " + String(correcta + 1) + "\n";
                confirmacion += "Puntos: " + String(puntos) + "\n";
                confirmacion += "Total preguntas: " + String(totalPreguntas);
                
                bot.sendMessage(chat_id, confirmacion, "");
              }
            }
          }
        }
      }
      else if (text == "/help" || text == "/start") {
        String helpMsg = "🎮 *QUIZ ESP32 - COMANDOS* 🎮\n\n";
        helpMsg += "👤 Usuarios:\n";
        helpMsg += "/ingresar_usuario NOMBRE - Crear usuario\n";
        helpMsg += "/usuarios - Ver todos los usuarios\n";
        helpMsg += "/estadisticas - Estadísticas por usuario\n\n";
        helpMsg += "🏆 Ranking:\n";
        helpMsg += "/ranking - Ver ranking ordenado\n\n";
        helpMsg += "❓ Preguntas:\n";
        helpMsg += "/preguntas - Ver todas las preguntas\n";
        helpMsg += "/agregar_pregunta - Agregar nueva pregunta\n\n";
        helpMsg += "💡 En el dispositivo:\n";
        helpMsg += "• Gira el encoder para navegar\n";
        helpMsg += "• Presiona el botón para seleccionar";
        
        bot.sendMessage(chat_id, helpMsg, "");
      }
      else {
        bot.sendMessage(chat_id, "❓ Comando no reconocido. Usa /help para ver comandos disponibles.", "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ==================== FUNCIONES DE PANTALLA ====================

void mostrarPantallaInicio() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("QUIZ MULTIJUGADOR");
  display.setCursor(0, 15);
  display.println("Bienvenidos!!");
  display.println("Click para comenzar");
  display.println("Ver telegram para Comandos");
  display.setCursor(0, 30);
  //display.println("Usuarios: " + String(totalUsuarios));
  display.setCursor(0, 45);
  //display.println("Preguntas: " + String(totalPreguntas));
  display.display();
}

void mostrarPantallaSeleccionUsuario() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SELECCIONA USUARIO:");

  if (totalUsuarios == 0) {
    display.setCursor(5, 20);
    display.println("No hay usuarios");
    display.setCursor(5, 35);
    display.println("Usa Telegram:");
    display.setCursor(5, 45);
    display.println("/ingresar_usuario");
  } else {
    for (int i = 0; i < totalUsuarios; i++) {
      display.setCursor(5, 15 + i * 12);
      if (i == opcionSeleccionada) {
        display.print("> ");
      } else {
        display.print("  ");
      }
      display.println(usuarios[i].nombre);
    }
  }
  display.display();
}

void mostrarPregunta() {
  display.clearDisplay();
  display.setCursor(0, 0);
  if (usuarioActual >= 0) {
    display.print(usuarios[usuarioActual].nombre);
  }
  display.setCursor(70, 0);
  display.print(preguntaActual + 1);
  display.print("/");
  display.print(totalPreguntas);
  display.setCursor(100, 0);
  display.print(puntuacionTotal);

  // Mostrar pregunta
  String texto = preguntas[preguntaActual].texto;
  int inicio = 0;
  int y = 12;
  
  while (inicio < texto.length()) {
    int fin = inicio + 21;
    if (fin > texto.length()) {
      fin = texto.length();
    }
    display.setCursor(0, y);
    display.println(texto.substring(inicio, fin));
    inicio = fin;
    y += 10;
  }

  // Mostrar opciones
  for (int i = 0; i < 3; i++) {
    display.setCursor(5, 35 + i * 10);
    if (i == opcionSeleccionada) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    String opcion = preguntas[preguntaActual].opciones[i];
    if (opcion.length() > 18) {
      opcion = opcion.substring(0, 18);
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

void actualizarPantalla() {
  switch(estadoActual) {
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
}

// ==================== FUNCIONES DEL QUIZ ====================

void verificarRespuesta() {
  bool correcta = (opcionSeleccionada == preguntas[preguntaActual].respuestaCorrecta);

  if (correcta) {
    puntuacionTotal += preguntas[preguntaActual].puntaje;
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
  }

  preguntaActual++;
  if (preguntaActual >= totalPreguntas) {
    quizCompletado = true;
    if (usuarioActual >= 0) {
      usuarios[usuarioActual].partidasJugadas++;
      if (puntuacionTotal > usuarios[usuarioActual].puntuacionMaxima) {
        usuarios[usuarioActual].puntuacionMaxima = puntuacionTotal;
      }
      guardarUsuarios();
      ordenarUsuariosPorRanking();
    }
    estadoActual = ESTADO_FINAL;
    mostrarResultado();
  } else {
    opcionSeleccionada = 0;
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
  Serial.println("Juego iniciado");
}

void reiniciarQuiz() {
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  quizCompletado = false;
  quizIniciado = false;
  estadoActual = ESTADO_INICIO;
  mostrarPantallaInicio();
}

// ==================== SETUP Y LOOP ====================

void setup() {
  Serial.begin(9600);
  Serial.println("Iniciando Quiz ESP32...");
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  
  // Configurar interrupciones
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_SW), buttonISR, FALLING);
  
  // Inicializar display
  if(!display.begin(0x3c, true)) {
    Serial.println("Error inicializando OLED");
    while(1);
  }
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();
  
  // Inicializar sistema
  inicializarSPIFFS();
  cargarPreguntas();
  cargarUsuarios();
  conectarTelegram();
  client.setServer(mqtt_server, mqtt_port);
  mostrarPantallaInicio();
  Serial.println("Sistema listo");
}

void loop() {
  // Procesar Telegram cada 200ms
  if (millis() > lastTimeBotRan + 200) {
    procesarComandosTelegram();
    lastTimeBotRan = millis();
  }
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Procesar encoder y botón (prioridad máxima)
  procesarEncoder();
  procesarBoton();

  delay(10); // Pequeño delay para estabilidad
}