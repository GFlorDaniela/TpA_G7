#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Pines
#define LED_PIN 23
#define ENCODER_CLK 18
#define ENCODER_DT 5
#define ENCODER_SW 19

// Estructuras de datos --> creo una estructura para que todo
// lo relacionado a la pregunta y al usuario este junto y pueda acceder facilmente
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

// SIMULACIÓN DE ARCHIVOS - Contenido de preguntas.txt --> en el real esto iria en un archivo
const char* ARCHIVO_PREGUNTAS[] = {
  "¿Qué lenguaje usa Arduino?;C++;Python;Java;0",
  "Capital de Francia;Roma;Madrid;París;2", 
  "Color bandera Argentina;Azul;Blanco;Celeste;2",
  "¿2+2?;3;4;5;1",
  "Animal Australia;Canguro;Koala;Emú;0"
};

// SIMULACIÓN DE ARCHIVOS - Contenido de puntajes.txt --> iria en un archivo 
const int ARCHIVO_PUNTAJES[] = {10, 15, 10, 5, 10};

// Variables globales
Pregunta preguntas[10]; // tengo un array con 10 preguntas, el struct 
Usuario usuarios[20]; // tengo un array con 20 usuarios como max 
int totalPreguntas = 5; // se muestran 5 de las 10 preguntas
int totalUsuarios = 0; // no hay ningun usuario registrado
int preguntaActual = 0; // en que pregunta estoy
int opcionSeleccionada = 0; // que opcion seleccione 
int puntuacionTotal = 0; // suma de la puntuacion obtenida
int usuarioActual = -1; // en que usuario estoy (-1 = ninguno)
bool quizCompletado = false; // se pone en true cuando se completa el quiz
bool quizIniciado = false; // se pone en true cuando giro el encoder para emprezar el quizz
bool ingresandoNombre = false; // true cuando estoy ingresando el nombre
String nombreTemp = ""; // aca se guarda el nombre que voy ingresando

// Variables encoder
int lastCLK = HIGH;
unsigned long lastButtonPress = 0;

// FUNCIONES DE SIMULACIÓN DE ARCHIVOS
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

//Funcion de menu que miestra disitontos tipos de partida que se pueden jugar: 1vs1, jugar contra el mejor ranking y multijugador(hasta 20 jugadores)
/*
// --- Función para mostrar el menú ---
void mostrarMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Selecciona modo:");
  
  String opciones[3] = {"1vs1", "Ranking", "Multijugador"};

  for (int i = 0; i < 3; i++) {
    if (i == opcionSeleccionada) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(opciones[i]);
  }

  display.display();
}
*/

// FUNCIONES DE USUARIOS (para Wokwi) .-> mi idea es no tener ninguno al inicio y 
// que se vayan guardando a medida que voy jugando 
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
  }
}

void mostrarRanking() {
  Serial.println("=== RANKING ACTUAL ===");
  
  Usuario rankingTemp[totalUsuarios];
  for(int i = 0; i < totalUsuarios; i++) {
    rankingTemp[i] = usuarios[i];
  }
  
  // ORDENAR MEJORADO --> aca me lo ordena primero x puntuacion maxima y 
  // si tienen la misma puntuacion, el que tiene mas partidas jugadas va primero
  for(int i = 0; i < totalUsuarios - 1; i++) {
    for(int j = i + 1; j < totalUsuarios; j++) {
      bool debeIntercambiar = false;
      
      // Criterio 1: Puntuación máxima
      if(rankingTemp[j].puntuacionMaxima > rankingTemp[i].puntuacionMaxima) {
        debeIntercambiar = true;
      }
      // Criterio 2: Si misma puntuación, más partidas
      else if(rankingTemp[j].puntuacionMaxima == rankingTemp[i].puntuacionMaxima) {
        if(rankingTemp[j].partidasJugadas > rankingTemp[i].partidasJugadas) {
          debeIntercambiar = true;
        }
        // Criterio 3: Si mismo todo, orden alfabético
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
    if(i < 9) posicion = " " + posicion; // Alinear números
    
    Serial.println(posicion + " " + rankingTemp[i].nombre + 
                  " - Puntos: " + rankingTemp[i].puntuacionMaxima +
                  " - Partidas: " + rankingTemp[i].partidasJugadas);
  }
  Serial.println("=====================");
}

// FUNCIONES DE INTERFAZ
void mostrarPantallaInicio() {
  display.clearDisplay();
  display.setCursor(10, 10);
  display.println("QUIZ ESP32");
  display.setCursor(5, 25);
  display.println("Wokwi Edition");
  display.setCursor(0, 45);
  display.println("Gira encoder empezar");
  display.display();
}

// pagina que se muestra cuando estoy ingresando el nombre 

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
  
  // Pregunta --> intento que se vean bien las preguntas largas --> no lo logro todavia 
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
  } else {
    display.setCursor(0, 30);
    display.println("INCORRECTO");
    display.setCursor(0, 45);
    display.print("Correcta: ");
    display.println(preguntas[preguntaActual].opciones[preguntas[preguntaActual].respuestaCorrecta]);
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
  mostrarPantallaInicio();
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
      
      // Actualizar última letra del nombre temporal --> quiero que se vayan guardando en un archivo ordenado Uusuario Puntaje NRanking --> en el real
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
    // Botón recién presionado
    if(!botonPresionado) {
      botonPresionado = true;
      inicioPresion = millis();
    }
    
    // Verificar si se mantuvo presionado por más de 1 segundo
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
      }
    }
  } else {
    // Botón liberado
    if(botonPresionado) {
      // Fue un CLICK CORTO - Agregar nueva letra
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
        // Si llegó al máximo, el click corto no hace nada
      }
      botonPresionado = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  
  display.begin(0x3c, true);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();
  
  simularCargaArchivos();
  inicializarUsuariosEjemplo();

  
  mostrarPantallaInicio();
  Serial.println("Quiz listo - Gira encoder para comenzar");

  //mostrar menu de opciones de partida antes de iniciar el juego: 1vs1, ranking y multijugador
  // mostrarMenu();

}

void loop() {
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

  //aca en el loop en esta parte se maneja el tema de la seleccion del tipo de partida
  /*

  Le faltaria que cuando se elige un 1vs1 solo se permita la carga de dos usuarios, que si se elegige jugar contra el mejor ranking seleccionado anteriormente 
  se muestre este y para el multijugador no haria falta nada mas que lo que ya tenemos

  // --- Navegación con botones ---
  if (digitalRead(BTN_UP) == LOW) {
    opcionSeleccionada = (opcionSeleccionada - 1 + 3) % 3;
    mostrarMenu();
    delay(200);
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    opcionSeleccionada = (opcionSeleccionada + 1) % 3;
    mostrarMenu();
    delay(200);
  }
  if (digitalRead(BTN_OK) == LOW) {
    String opciones[3] = {"1vs1", "ranking", "multijugador"};
    modoPartida = opciones[opcionSeleccionada];
    client.publish(topic, ("modo:" + modoPartida).c_str());
    Serial.println("Modo elegido manualmente: " + modoPartida);
    delay(200);
  }

  // Si ya se eligió modo, mostrar confirmación
  if (modoPartida != "") {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.print("Modo seleccionado:");
    display.setCursor(30, 40);
    display.print(modoPartida);
    display.display();
  }
  */
  
  if(!quizIniciado) {
    if(currentCLK != lastCLK) {
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