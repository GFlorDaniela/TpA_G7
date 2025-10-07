#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// LED
#define LED_PIN 23

// Encoder (ejemplo con pines de tu JSON)
#define ENCODER_CLK 18
#define ENCODER_DT 5
#define ENCODER_SW 19

// Preguntas hardcodeadas (porque Wokwi no soporta SPIFFS)
const char* preguntas[] = {
  "¿Capital de Francia?\n- Paris\n- Roma\n- Berlin",
  "¿2+2?\n- 3\n- 4\n- 5",
  "¿Color del cielo?\n- Azul\n- Verde\n- Rojo"
};
int totalPreguntas = sizeof(preguntas) / sizeof(preguntas[0]);

int preguntaActual = 0;
int opcionSeleccionada = 0;

int lastCLK = HIGH;

void mostrarPregunta() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(preguntas[preguntaActual]);

  // Mostrar selector ">"
  int y = 10 + opcionSeleccionada * 10;
  display.setCursor(0, y);
  display.print(">");
  display.display();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  display.begin(0x3c, true);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  display.clearDisplay();
  display.setCursor(10, 20);
  display.println("Quizz Archivos ESP32");
  display.display();
  delay(2000);

  mostrarPregunta();
}

void loop() {
  int currentCLK = digitalRead(ENCODER_CLK);

  if (currentCLK != lastCLK) {
    if (digitalRead(ENCODER_DT) != currentCLK) {
      opcionSeleccionada++;
    } else {
      opcionSeleccionada--;
    }
    if (opcionSeleccionada < 0) opcionSeleccionada = 0;
    if (opcionSeleccionada > 2) opcionSeleccionada = 2;
    mostrarPregunta();
  }
  lastCLK = currentCLK;

  if (digitalRead(ENCODER_SW) == LOW) {
    // Confirmar respuesta (por ahora solo avanzar pregunta)
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);

    preguntaActual++;
    if (preguntaActual >= totalPreguntas) preguntaActual = 0;
    opcionSeleccionada = 0;
    mostrarPregunta();
    delay(300);
  }
}