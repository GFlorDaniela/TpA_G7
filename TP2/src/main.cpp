#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
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
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;

// Telegram
const String BOT_TOKEN = "8376405384:AAH_30BV0A7zlZotdfKpx3KucxvUtSanau8";  // REEMPLAZA CON TU TOKEN
const String CHAT_ID = "1307295110";      // REEMPLAZA CON TU CHAT ID

WiFiClient client;
PubSubClient mqttClient(client);
UniversalTelegramBot bot(BOT_TOKEN, client);

// ==================== ESTRUCTURAS DE DATOS ====================
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

// SIMULACIÓN DE ARCHIVOS - Contenido de preguntas.txt
const char* ARCHIVO_PREGUNTAS[] = {
  "¿Qué lenguaje usa Arduino?;C++;Python;Java;0",
  "Capital de Francia;Roma;Madrid;París;2", 
  "Color bandera Argentina;Azul;Blanco;Celeste;2",
  "¿2+2?;3;4;5;1",
  "Animal Australia;Canguro;Koala;Emú;0"
};

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
bool quizCompletado = false;
bool quizIniciado = false;
bool ingresandoNombre = false;
String nombreTemp = "";
String modoPartida = "";  // Para almacenar el modo de partida

// Variables encoder
int lastCLK = HIGH;
unsigned long lastButtonPress = 0;

// Variables Telegram
unsigned long lastTimeBotRan = 0;
bool telegramConnected = false;

// ==================== PROTOTIPOS DE FUNCIONES ====================
void mostrarIngresoNombre();
void mostrarPantallaInicio();
void mostrarPregunta();
void simularCargaArchivos();
void inicializarUsuariosEjemplo();
int buscarUsuario(String nombre);
void agregarUsuario(String nombre);
void actualizarPuntuacionUsuario();
void mostrarRanking();
void mostrarResultado();
void verificarRespuesta();
void reiniciarQuiz();
void procesarEntradaNombre();
void mostrarTextoEnLineas(String texto, int x, int y, int anchoMax);
String limpiarTexto(String texto);

// ==================== FUNCIONES MQTT ====================

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  Serial.print("📨 Mensaje MQTT recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String mensaje = "";
  for (int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }
  Serial.println(mensaje);
  
  // Procesar comandos del bot de Telegram
  if (mensaje == "iniciar_partida") {
    Serial.println("🎮 Comando: iniciar_partida");
    if (modoPartida != "" && !quizIniciado && !ingresandoNombre) {
      ingresandoNombre = true;
      nombreTemp = "A";
      mostrarIngresoNombre();
    }
  } 
  else if (mensaje.startsWith("modo:")) {
    String nuevoModo = mensaje.substring(5);
    nuevoModo.toLowerCase();
    
    if (nuevoModo == "1vs1" || nuevoModo == "ranking" || nuevoModo == "multijugador") {
      modoPartida = nuevoModo;
      Serial.println("🎯 Modo MQTT recibido: " + modoPartida);
      
      // Actualizar pantalla
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("MODO SELECCIONADO:");
      display.setCursor(0, 20);
      display.println(modoPartida);
      display.setCursor(0, 40);
      display.println("Gira encoder empezar");
      display.display();
      
      // Notificar por Telegram
      if (telegramConnected) {
        bot.sendMessage(CHAT_ID, "✅ Modo recibido en ESP32: *" + modoPartida + "*", "Markdown");
      }
    }
  }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("🔌 Conectando a MQTT...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("✅ Conectado MQTT!");
      mqttClient.subscribe("wokwi/acciones");
      Serial.println("📡 Suscrito a: wokwi/acciones");
    } else {
      Serial.print("❌ Falló, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" reintento en 5s");
      delay(5000);
    }
  }
}

// ==================== FUNCIONES TELEGRAM ====================

void conectarTelegram() {
  Serial.println("📶 Conectando a WiFi...");
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
    Serial.println("\n✅ Conectado a WiFi!");
    Serial.print("📡 IP: ");
    Serial.println(WiFi.localIP());
    
    // Configurar MQTT después de WiFi
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(callbackMQTT);
    
    // Verificar conexión con Telegram
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Conectando Telegram...");
    display.display();
    
    int botUsuarios = bot.getUpdates(bot.last_message_received + 1);
    if (botUsuarios != -1) {
      telegramConnected = true;
      Serial.println("✅ Conexión con Telegram exitosa!");
      
      // Enviar mensaje de inicio
      bot.sendMessage(CHAT_ID, "🤖 ¡Bot del Quiz ESP32 conectado! 🎮", "");
      bot.sendMessage(CHAT_ID, "Usa /seleccionar_partida para elegir modo de juego", "");
      
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Telegram OK!");
      display.setCursor(0, 20);
      display.println("Usa /seleccionar_partida");
      display.display();
      delay(2000);
    } else {
      Serial.println("❌ Error en conexión Telegram");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Error Telegram");
      display.display();
      delay(2000);
    }
  } else {
    Serial.println("\n❌ Error conectando a WiFi");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Error WiFi");
    display.display();
    delay(2000);
  }
}

void procesarComandosTelegram() {
  if (!telegramConnected) {
    Serial.println("❌ Telegram no conectado");
    return;
  }

  Serial.println("🔍 Buscando mensajes de Telegram...");
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  Serial.print("📱 Mensajes encontrados: ");
  Serial.println(numNewMessages);
  
  while (numNewMessages) {
    Serial.println("💬 Mensaje recibido de Telegram");
    
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;
      
      Serial.print("👤 Chat ID: ");
      Serial.println(chat_id);
      Serial.print("📝 Mensaje: ");
      Serial.println(text);
      Serial.print("✅ Chat ID esperado: ");
      Serial.println(CHAT_ID);
      
      // Solo responder al chat ID autorizado
      if (chat_id == CHAT_ID) {
        Serial.println("🎯 Mensaje autorizado - procesando...");
        // ... resto del código de comandos
      } else {
        Serial.println("❌ Chat ID no autorizado");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

/*
void procesarComandosTelegram() {
  if (!telegramConnected) return;

  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  
  while (numNewMessages) {
    Serial.println("💬 Mensaje recibido de Telegram");
    
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;
      
      Serial.println("Mensaje: " + text);
      
      // Solo responder al chat ID autorizado
      if (chat_id == CHAT_ID) {
        if (text == "/start" || text == "/help") {
          String welcome = "🎮 *Quiz ESP32 - Comandos disponibles:*\n";
          welcome += "/seleccionar_partida - Elegir modo de juego\n";
          welcome += "/iniciar_partida - Comenzar partida\n";
          welcome += "/ranking - Ver ranking actual\n";
          welcome += "/estado - Estado actual del juego\n";
          welcome += "/help - Ver esta ayuda";
          bot.sendMessage(chat_id, welcome, "Markdown");
        }
        else if (text == "/estado") {
          String estado = "📊 *Estado Actual:*\n";
          estado += "Modo: " + (modoPartida == "" ? "No seleccionado" : modoPartida) + "\n";
          estado += "Quiz: " + String(quizIniciado ? "En curso" : "No iniciado") + "\n";
          estado += "Pregunta: " + String(preguntaActual + 1) + "/" + String(totalPreguntas) + "\n";
          estado += "Puntuación: " + String(puntuacionTotal) + "\n";
          estado += "Usuario: " + (usuarioActual >= 0 ? usuarios[usuarioActual].nombre : "No seleccionado");
          bot.sendMessage(chat_id, estado, "Markdown");
        }
        else if (text == "/ranking") {
          String rankingMsg = "🏆 *Ranking Actual:*\n";
          for(int i = 0; i < (totalUsuarios < 5 ? totalUsuarios : 5); i++) {
            rankingMsg += String(i+1) + ". " + usuarios[i].nombre + " - " + 
                         String(usuarios[i].puntuacionMaxima) + " pts\n";
          }
          if (totalUsuarios == 0) {
            rankingMsg += "No hay usuarios registrados";
          }
          bot.sendMessage(chat_id, rankingMsg, "Markdown");
        }
        else if (text.startsWith("/seleccionar_partida")) {
          if (text.length() > 21) {
            String modo = text.substring(21);
            modo.toLowerCase();
            modo.trim();
            
            if (modo == "1vs1" || modo == "ranking" || modo == "multijugador") {
              modoPartida = modo;
              bot.sendMessage(chat_id, "✅ Modo seleccionado: *" + modo + "*", "Markdown");
              
              // Enviar modo por MQTT al ESP32
              if (mqttClient.connected()) {
                mqttClient.publish("wokwi/acciones", ("modo:" + modo).c_str());
                Serial.println("📤 Modo enviado por MQTT: " + modo);
              }
              
              // Actualizar pantalla
              display.clearDisplay();
              display.setCursor(0, 0);
              display.println("MODO SELECCIONADO:");
              display.setCursor(0, 20);
              display.println(modo);
              display.setCursor(0, 40);
              display.println("Gira encoder empezar");
              display.display();
              
              Serial.println("Modo de partida seleccionado: " + modo);
            } else {
              bot.sendMessage(chat_id, "❌ Modos válidos: 1vs1, ranking, multijugador", "Markdown");
            }
          } else {
            String ayuda = "🎮 *Selecciona modo de partida:*\n";
            ayuda += "/seleccionar_partida 1vs1\n";
            ayuda += "/seleccionar_partida ranking\n"; 
            ayuda += "/seleccionar_partida multijugador";
            bot.sendMessage(chat_id, ayuda, "Markdown");
          }
        }
        else if (text == "/iniciar_partida") {
          if (modoPartida == "") {
            bot.sendMessage(chat_id, "❌ Primero selecciona un modo con /seleccionar_partida", "Markdown");
          } else if (!quizIniciado && !ingresandoNombre) {
            bot.sendMessage(chat_id, "🎮 Iniciando partida en modo: *" + modoPartida + "*", "Markdown");
            
            // Enviar comando por MQTT al ESP32
            if (mqttClient.connected()) {
              mqttClient.publish("wokwi/acciones", "iniciar_partida");
              Serial.println("📤 Comando enviado por MQTT: iniciar_partida");
            }
          } else {
            bot.sendMessage(chat_id, "⚠️ El juego ya está en curso", "Markdown");
          }
        }
        else {
          bot.sendMessage(chat_id, "❌ Comando no reconocido. Usa /help para ver comandos disponibles.", "Markdown");
        }
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}
*/


// ==================== FUNCIONES EXISTENTES DEL QUIZ ====================

void simularCargaArchivos() {
  Serial.println("=== SIMULANDO SISTEMA DE ARCHIVOS ===");
  Serial.println("Cargando preguntas desde 'preguntas.txt'...");
  Serial.println("Cargando puntajes desde 'puntajes.txt'...");
  
  for(int i = 0; i < totalPreguntas; i++){
    String linea = ARCHIVO_PREGUNTAS[i];
    
    int separadores[5];
    int sepIndex = 0;
    
    for(int j = 0; j < linea.length() && sepIndex < 5; j++){
      if(linea.charAt(j) == ';'){
        separadores[sepIndex++] = j;
      }
    }
    
    if(sepIndex == 4){
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

void inicializarUsuariosEjemplo() {
  // Agregar algunos usuarios de ejemplo
  usuarios[0] = {"ANA", 45, 3};
  usuarios[1] = {"CARLOS", 35, 2};
  usuarios[2] = {"MARIA", 50, 4};
  totalUsuarios = 3;
  
  Serial.println("Usuarios de ejemplo cargados:");
  for(int i = 0; i < totalUsuarios; i++) {
    Serial.println(usuarios[i].nombre + " - Max: " + usuarios[i].puntuacionMaxima + 
                  " - Partidas: " + usuarios[i].partidasJugadas);
  }
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
  if(totalUsuarios < 20) {
    usuarios[totalUsuarios].nombre = nombre;
    usuarios[totalUsuarios].puntuacionMaxima = 0;
    usuarios[totalUsuarios].partidasJugadas = 0;
    usuarioActual = totalUsuarios;
    totalUsuarios++;
    Serial.println("Nuevo usuario creado: " + nombre);
    
    // Notificar a Telegram
    if (telegramConnected) {
      bot.sendMessage(CHAT_ID, "👤 Nuevo usuario registrado: *" + nombre + "*", "Markdown");
    }
  }
}

void actualizarPuntuacionUsuario() {
  if(usuarioActual >= 0) {
    usuarios[usuarioActual].partidasJugadas++;
    if(puntuacionTotal > usuarios[usuarioActual].puntuacionMaxima) {
      usuarios[usuarioActual].puntuacionMaxima = puntuacionTotal;
    }
    Serial.println("Usuario " + usuarios[usuarioActual].nombre + 
                  " actualizado. Max: " + usuarios[usuarioActual].puntuacionMaxima +
                  " Partidas: " + usuarios[usuarioActual].partidasJugadas);
    
    // Notificar a Telegram
    if (telegramConnected) {
      String msg = "🏆 *Actualización de Usuario:*\n";
      msg += "Jugador: " + usuarios[usuarioActual].nombre + "\n";
      msg += "Puntuación máxima: " + String(usuarios[usuarioActual].puntuacionMaxima) + "\n";
      msg += "Partidas jugadas: " + String(usuarios[usuarioActual].partidasJugadas);
      bot.sendMessage(CHAT_ID, msg, "Markdown");
    }
  }
}

void mostrarRanking() {
  Serial.println("=== RANKING ACTUAL ===");
  
  Usuario rankingTemp[totalUsuarios];
  for(int i = 0; i < totalUsuarios; i++) {
    rankingTemp[i] = usuarios[i];
  }
  
  // ORDENAR MEJORADO
  for(int i = 0; i < totalUsuarios - 1; i++) {
    for(int j = i + 1; j < totalUsuarios; j++) {
      bool debeIntercambiar = false;
      
      if(rankingTemp[j].puntuacionMaxima > rankingTemp[i].puntuacionMaxima) {
        debeIntercambiar = true;
      }
      else if(rankingTemp[j].puntuacionMaxima == rankingTemp[i].puntuacionMaxima) {
        if(rankingTemp[j].partidasJugadas > rankingTemp[i].partidasJugadas) {
          debeIntercambiar = true;
        }
        else if(rankingTemp[j].partidasJugadas == rankingTemp[i].partidasJugadas) {
          if(rankingTemp[j].nombre < rankingTemp[i].nombre) {
            debeIntercambiar = true;
          }
        }
      }
      
      if(debeIntercambiar) {
        Usuario temp = rankingTemp[i];
        rankingTemp[i] = rankingTemp[j];
        rankingTemp[j] = temp;
      }
    }
  }
  
  // MOSTRAR
  for(int i = 0; i < totalUsuarios; i++) {
    String posicion = String(i+1) + ".";
    if(i < 9) posicion = " " + posicion;
    
    Serial.println(posicion + " " + rankingTemp[i].nombre + 
                  " - Puntos: " + rankingTemp[i].puntuacionMaxima +
                  " - Partidas: " + rankingTemp[i].partidasJugadas);
  }
  Serial.println("=====================");
}

void mostrarPantallaInicio() {
  display.clearDisplay();
  display.setCursor(10, 10);
  display.println("QUIZ ESP32");
  display.setCursor(5, 25);
  display.println("Telegram Bot");
  
  if (modoPartida != "") {
    display.setCursor(0, 40);
    display.print("Modo: ");
    display.println(modoPartida);
    display.setCursor(0, 55);
    display.println("Gira encoder empezar");
  } else {
    display.setCursor(0, 45);
    display.println("Usa Telegram para");
    display.setCursor(0, 55);
    display.println("seleccionar modo");
  }
  display.display();
}

void mostrarIngresoNombre() {
  display.clearDisplay();
  display.setCursor(10, 10);
  display.println("INGRESA TU NOMBRE");
  display.setCursor(0, 25);
  display.print("Nombre: ");
  display.println(nombreTemp);
  display.setCursor(0, 35);
  display.print("Letra actual: ");
  display.print(char(nombreTemp.length() > 0 ? nombreTemp.charAt(nombreTemp.length()-1) : 'A'));
  display.setCursor(0, 45);
  display.print("Click: nueva letra");
  display.setCursor(0, 55);
  display.print("Mantén>1s: FINALIZAR");
  display.display();
}

void mostrarTextoEnLineas(String texto, int x, int y, int anchoMax) {
  int inicio = 0;
  int fin = anchoMax / 6;
  
  while (inicio < texto.length()) {
    if (fin > texto.length()) fin = texto.length();
    
    int ultimoEspacio = texto.lastIndexOf(' ', fin);
    if (ultimoEspacio == -1 || ultimoEspacio <= inicio) ultimoEspacio = fin;
    
    display.setCursor(x, y);
    display.println(texto.substring(inicio, ultimoEspacio));
    
    y += 10;
    inicio = ultimoEspacio + 1;
    fin = inicio + (anchoMax / 6);
  }
}

String limpiarTexto(String texto) {
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

void mostrarPregunta() {
  display.clearDisplay();
  
  // Header con usuario, progreso y puntuación
  display.setCursor(0, 0);
  if(usuarioActual >= 0) {
    display.print(usuarios[usuarioActual].nombre);
  } else {
    display.print("JUGADOR");
  }
  display.setCursor(70, 0);
  display.print(preguntaActual + 1);
  display.print("/");
  display.print(totalPreguntas);
  display.setCursor(100, 0);
  display.print(puntuacionTotal);
  
  // Pregunta
  mostrarTextoEnLineas(limpiarTexto(preguntas[preguntaActual].texto), 0, 12, 128);
  
  // Opciones con selector
  for(int i = 0; i < 3; i++) {
    display.setCursor(5, 25 + i * 12);
    if(i == opcionSeleccionada) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(preguntas[preguntaActual].opciones[i]);
  }
  
  display.display();
}

void mostrarResultado() {
  display.clearDisplay();
  display.setCursor(20, 10);
  display.println("QUIZ COMPLETADO!");
  display.setCursor(10, 25);
  if(usuarioActual >= 0) {
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

void verificarRespuesta() {
  bool correcta = (opcionSeleccionada == preguntas[preguntaActual].respuestaCorrecta);
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(preguntas[preguntaActual].texto);
  display.setCursor(0, 15);
  display.print("Elegiste: ");
  display.println(preguntas[preguntaActual].opciones[opcionSeleccionada]);
  
  if(correcta) {
    puntuacionTotal += preguntas[preguntaActual].puntaje;
    display.setCursor(0, 30);
    display.print("CORRECTO! +");
    display.print(preguntas[preguntaActual].puntaje);
    digitalWrite(LED_PIN, HIGH);
    
    // Notificar a Telegram
    if (telegramConnected) {
      bot.sendMessage(CHAT_ID, "✅ ¡Respuesta correcta! +" + 
                     String(preguntas[preguntaActual].puntaje) + " puntos", "");
    }
  } else {
    display.setCursor(0, 30);
    display.println("INCORRECTO");
    display.setCursor(0, 45);
    display.print("Correcta: ");
    display.println(preguntas[preguntaActual].opciones[preguntas[preguntaActual].respuestaCorrecta]);
    
    // Notificar a Telegram
    if (telegramConnected) {
      bot.sendMessage(CHAT_ID, "❌ Respuesta incorrecta. La correcta era: " + 
                     preguntas[preguntaActual].opciones[preguntas[preguntaActual].respuestaCorrecta], "");
    }
  }
  
  display.display();
  delay(3000);
  digitalWrite(LED_PIN, LOW);
  
  preguntaActual++;
  if(preguntaActual >= totalPreguntas) {
    quizCompletado = true;
    actualizarPuntuacionUsuario();
    mostrarRanking();
    mostrarResultado();
    
    // Enviar resultado final a Telegram
    if (telegramConnected) {
      String resultado = "🏁 *QUIZ COMPLETADO!*\n";
      resultado += "Jugador: " + (usuarioActual >= 0 ? usuarios[usuarioActual].nombre : "Anónimo") + "\n";
      resultado += "Puntuación final: *" + String(puntuacionTotal) + "* puntos\n";
      resultado += "Modo de juego: " + modoPartida;
      bot.sendMessage(CHAT_ID, resultado, "Markdown");
    }
  } else {
    opcionSeleccionada = 0;
    mostrarPregunta();
  }
}

void reiniciarQuiz() {
  preguntaActual = 0;
  opcionSeleccionada = 0;
  puntuacionTotal = 0;
  quizCompletado = false;
  quizIniciado = false;
  ingresandoNombre = false;
  nombreTemp = "";
  usuarioActual = -1;
  // No reiniciamos modoPartida para mantener la selección
  mostrarPantallaInicio();
  
  // Notificar a Telegram
  if (telegramConnected) {
    bot.sendMessage(CHAT_ID, "🔄 Quiz reiniciado. Listo para nueva partida en modo: *" + modoPartida + "*", "Markdown");
  }
}

void procesarEntradaNombre() {
  static char letraActual = 'A';
  static unsigned long ultimoGiro = 0;
  static unsigned long inicioPresion = 0;
  static bool botonPresionado = false;
  
  int currentCLK = digitalRead(ENCODER_CLK);
  
  // Manejo del encoder para cambiar letras
  if(currentCLK != lastCLK && currentCLK == LOW) {
    if(millis() - ultimoGiro > 150) {
      if(digitalRead(ENCODER_DT) == LOW) {
        letraActual++;
      } else {
        letraActual--;
      }
      
      if(letraActual < 'A') letraActual = 'Z';
      if(letraActual > 'Z') letraActual = 'A';
      
      if(nombreTemp.length() > 0) {
        nombreTemp.remove(nombreTemp.length() - 1);
      }
      nombreTemp += String(letraActual);
      
      mostrarIngresoNombre();
      ultimoGiro = millis();
    }
  }
  lastCLK = currentCLK;
  
  // Manejo del botón
  if(digitalRead(ENCODER_SW) == LOW) {
    if(!botonPresionado) {
      botonPresionado = true;
      inicioPresion = millis();
    }
    
    if(botonPresionado && (millis() - inicioPresion > 1000)) {
      // PRESIÓN LARGA - Finalizar nombre
      if(nombreTemp.length() > 0) {
        int usuarioExistente = buscarUsuario(nombreTemp);
        if(usuarioExistente == -1) {
          agregarUsuario(nombreTemp);
        } else {
          usuarioActual = usuarioExistente;
          Serial.println("Usuario existente: " + nombreTemp);
        }
        
        ingresandoNombre = false;
        quizIniciado = true;
        botonPresionado = false;
        mostrarPregunta();
        
        // Notificar a Telegram
        if (telegramConnected) {
          bot.sendMessage(CHAT_ID, "🎮 Partida iniciada!\nJugador: *" + nombreTemp + "*\nModo: *" + modoPartida + "*", "Markdown");
        }
      }
    }
  } else {
    if(botonPresionado) {
      if(millis() - inicioPresion <= 1000) {
        if(nombreTemp.length() == 0) {
          nombreTemp = "A";
          letraActual = 'A';
          mostrarIngresoNombre();
        } else if(nombreTemp.length() < 8) {
          nombreTemp += "A";
          letraActual = 'A';
          mostrarIngresoNombre();
        }
      }
      botonPresionado = false;
    }
  }
}

void setup() {
  Serial.begin(9600);
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  
  display.begin(0x3c, true);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();
  
  // Conectar a Telegram
  conectarTelegram();
  
  simularCargaArchivos();
  inicializarUsuariosEjemplo();
  
  mostrarPantallaInicio();
  Serial.println("🚀 Sistema listo. Usa Telegram para controlar.");
}

void loop() {
  // Mantener conexión MQTT
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
  
  // Procesar comandos de Telegram cada segundo
  if (millis() > lastTimeBotRan + 1000) {
    procesarComandosTelegram();
    lastTimeBotRan = millis();
  }
  
  int currentCLK = digitalRead(ENCODER_CLK);
  
  if(ingresandoNombre) {
    procesarEntradaNombre();
    return;
  }
  
  if(quizCompletado) {
    if(digitalRead(ENCODER_SW) == LOW && millis() - lastButtonPress > 500) {
      lastButtonPress = millis();
      reiniciarQuiz();
    }
    return;
  }
  
  if(!quizIniciado) {
    if(currentCLK != lastCLK && modoPartida != "") {
      ingresandoNombre = true;
      nombreTemp = "A";
      mostrarIngresoNombre();
    }
    lastCLK = currentCLK;
    return;
  }
  
  // Navegación normal del quiz
  if(currentCLK != lastCLK && currentCLK == LOW) {
    if(digitalRead(ENCODER_DT) == LOW) {
      opcionSeleccionada++;
    } else {
      opcionSeleccionada--;
    }
    
    if(opcionSeleccionada < 0) opcionSeleccionada = 2;
    if(opcionSeleccionada > 2) opcionSeleccionada = 0;
    
    mostrarPregunta();
  }
  lastCLK = currentCLK;
  
  if(digitalRead(ENCODER_SW) == LOW && millis() - lastButtonPress > 300) {
    lastButtonPress = millis();
    verificarRespuesta();
  }
}