#include <Device.h>

const byte LED = 23;
const byte ENC_PUSH = 19;
const byte POT = 32;
const byte ENC_CLK = 18;
const byte ENC_DT = 5;
bool pantalla = true;
bool menu = true;
Device device = Device(128, 64, -1, 33, DHT22);
volatile float hum_min = random(40, 60);
volatile int cont = 0;
int pasar = 0;
float t_ref = random(15,25);
bool forzarVentilacion = false;
bool forzarRiego = false;

bool ventilacionActiva = false;
bool riegoActivo = false;
bool t_forz = false;
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 1000;

// Variables para interrupciones
volatile int lastCLKState;
volatile bool encoderChanged = false;
unsigned long lastEncoderTime = 0;
const unsigned long encoderDebounce = 50;

// Función de interrupción para el encoder - SOLO en menú principal
void IRAM_ATTR handleEncoder() {
  if (menu) {  // ⭐⭐ Solo funciona en menú principal ⭐⭐
    if (millis() - lastEncoderTime > encoderDebounce) {
      int clkValue = digitalRead(ENC_CLK);
      int dtValue = digitalRead(ENC_DT);
      
      if (clkValue != lastCLKState) {
        if (dtValue != clkValue) {
          cont++;
        } else {
          cont--;
        }
        // Mantener contador entre 0-3
        if (cont > 3) cont = 0;
        if (cont < 0) cont = 3;
        
        encoderChanged = true;
        lastEncoderTime = millis();
      }
      lastCLKState = clkValue;
    }
  }
}

void setup() {
  Serial.begin(9600);
  device.begin();
  pinMode(LED, OUTPUT);
  pinMode(ENC_PUSH, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);

  // Configurar interrupciones
  lastCLKState = digitalRead(ENC_CLK);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), handleEncoder, CHANGE);
  
  char c_hum[40];
  sprintf(c_hum, "Humedad mínima: %.2f %", hum_min);
  Serial.println(c_hum);
  device.showDisplay("Sistema iniciando...", 0, 15);
  delay(2000);
  device.clear();
  device.mostrarMenu(cont);
}

void loop() {
 
  
  float hum = device.readHum();
  float t = device.readTemp();

  // ⭐⭐ Solo actualizar menú si estamos en menú principal ⭐⭐
  if (encoderChanged && menu) {
    device.clear();
    device.mostrarMenu(cont);
    encoderChanged = false;
  }
  
  if (menu) {
    if (!digitalRead(ENC_PUSH)) {
      delay(300);
      menu = false;
      device.clear();
      // ⭐⭐ Resetear variables al entrar a una opción ⭐⭐
      if (cont == 0) {
        pantalla = true;  // Siempre empezar con temperatura
        pasar = 0;
      }
    }
  } else {
    if (!forzarVentilacion) {
      ventilacionActiva = (t >= t_ref);
    }
    if (!forzarRiego) {
      riegoActivo = (hum < hum_min);
    }
    delay(200);
    switch (cont) {
      case 0:
{
  static unsigned long lastRefresh = 0;
  static unsigned long lastLedChange = 0;
  static bool ledState = false;
  const unsigned long refreshInterval = 800; // Refrescar cada 800ms
  
  // Refrescar pantalla solo cada cierto tiempo
  if (millis() - lastRefresh > refreshInterval) {
    lastRefresh = millis();
    
    device.clear();
  if (!t_forz) {
    t_ref = analogRead(POT);
    t_ref = map(t_ref, 0, 4095, 20.0, 40.0);
  }
    if (pantalla) {
      // MODO TEMPERATURA
      char texto[64];
      sprintf(texto, "Temp: %.1f C", t);
      device.showDisplay(texto, 0, 10);
      device.showDisplay("Ref: " + String(t_ref, 1) + " C", 0, 25);
      
      if (ventilacionActiva) {
        device.dibujarSol();
        device.showDisplay("Vent: ON", 0, 45);
      } else {
        device.dibujarCheck();
        device.showDisplay("Vent: OFF", 0, 45);
      }
    } else {
      // MODO HUMEDAD
      char texto[64];
      sprintf(texto, "Hum: %.1f %%", hum);
      device.showDisplay(texto, 0, 10);
      device.dibujarGota(hum);
      device.showDisplay("Min: " + String(hum_min, 1) + " %", 0, 25);
      
      if (riegoActivo) {
        device.showDisplay("Riego: ON", 0, 45);
      } else {
        device.showDisplay("Riego: OFF", 0, 45);
      }
    }
  }
  
  // Control de LED no bloqueante
  if (riegoActivo && !pantalla) {
    // Parpadeo para riego activo
    if (millis() - lastLedChange > 500) {
      lastLedChange = millis();
      ledState = !ledState;
      digitalWrite(LED, ledState ? HIGH : LOW);
    }
  } 
  else if (ventilacionActiva && pantalla) {
    // LED fijo ON para ventilación
    digitalWrite(LED, HIGH);
  } 
  else {
    // LED OFF
    digitalWrite(LED, LOW);
  }
  
  // Detectar botón
  if (!digitalRead(ENC_PUSH)) {
    delay(100); // Debounce corto
    pantalla = !pantalla;
    pasar++;
    
    // Forzar refresco inmediato
    lastRefresh = 0;
    device.clear();
    
    if (pasar >= 2) {
      pasar = 0;
      menu = true;
      digitalWrite(LED, LOW);
      device.clear();
      device.mostrarMenu(cont);
    }
  }
}
break;
        
      case 1:
        if (millis() - lastUpdate > updateInterval) {
          lastUpdate = millis();
          device.clear();
           if (!t_forz) {
            t_ref = analogRead(POT);
            t_ref = map(t_ref, 0, 4095, 20.0, 40.0);
          }
          // ⭐⭐ MEJOR VISUALIZACIÓN INFO COMPLETA ⭐⭐
          device.showDisplay("INFO COMPLETA", 0, 0);
          device.showDisplay("T:" + String(t, 1) + "C R:" + String(t_ref, 1) + "C", 0, 12);
          device.showDisplay("H:" + String(hum, 1) + "% M:" + String(hum_min, 1) + "%", 0, 24);
          
          // Estado sistemas más compacto
          if (ventilacionActiva & riegoActivo) {
          device.showDisplay("Vent ON", 0, 52);
          device.showDisplay("Riego ON", 64, 52);
          digitalWrite(LED, HIGH);
          delay(500);
          digitalWrite(LED, LOW);
          delay(150);
          digitalWrite(LED, HIGH);
          
        }
        else if (ventilacionActiva) {
          device.showDisplay("Vent ON", 0, 52);
          device.showDisplay("Riego OFF", 64, 52);
          digitalWrite(LED, HIGH);
        }
        else if (riegoActivo) {
          device.showDisplay("Vent OFF", 0, 52);
          device.showDisplay("Riego ON", 64, 52); 
          digitalWrite(LED, HIGH);
          delay(400);
          digitalWrite(LED, LOW);
          delay(200);
        }
        else {
          device.showDisplay("Vent OFF", 0, 52);
          device.showDisplay("Riego OFF", 64, 52);
          digitalWrite(LED, LOW);
        }
        }
        
        if (!digitalRead(ENC_PUSH)) {
          delay(300);
          menu = true;
          digitalWrite(LED, LOW);
          device.clear();
          device.mostrarMenu(cont);
        }
        break;
        
      case 2:
        // ⭐⭐ MEJOR VISUALIZACIÓN CONFIG SERIAL ⭐⭐
        device.clear();
        device.showDisplay("CONFIG SERIAL", 0, 0);
        device.showDisplay("TRef:" + String(t_ref,1) + "C HRef:" + String(hum_min,1) + "%", 0, 12);
        
        if (Serial.available()) {
          delay(2000);
          String input = Serial.readString();
          input.trim();
          if (input.startsWith("T=")) {
            t_forz = true;
            t_ref = input.substring(2).toFloat();
            device.showDisplay("Nueva T: " + String(t_ref,1), 0, 48);
            Serial.println("Nueva T ref: " + String(t_ref));
          } else if (input.startsWith("H=")) {
            hum_min = input.substring(2).toFloat();
            device.showDisplay("Nueva H: " + String(hum_min,1), 0, 48);
            Serial.println("Nueva H min: " + String(hum_min));
          }
          else if (input.startsWith("POT")) {
            t_forz = false;
            device.showDisplay("Usando POT", 0, 48);
            Serial.println("T ref por potenciometro");
          }
          // delay(1000);
        }

        
        
        if (!digitalRead(ENC_PUSH)) {
          delay(300);
          menu = true;
          device.clear();
          device.mostrarMenu(cont);
        }
        break;
        
      case 3:
        // ⭐⭐ MEJOR VISUALIZACIÓN FORZAR SISTEMAS ⭐⭐
        device.clear();
        device.showDisplay("FORZAR SISTEMAS", 0, 0);
        device.showDisplay("V:" + String(forzarVentilacion ? (ventilacionActiva ? "ON" : "OFF") : "AUTO"), 0, 15);
        device.showDisplay("R:" + String(forzarRiego ? (riegoActivo ? "ON" : "OFF") : "AUTO"), 64, 15);
        device.showDisplay("VENT_ON", 0, 45);
        device.showDisplay("VENT_OFF", 0, 55);
        device.showDisplay("RIEGO_ON", 64, 45);
        device.showDisplay("RIEGO_OFF", 64, 55);
        device.showDisplay("AUTO", 40, 30);

        if (Serial.available()) {
          String input = Serial.readStringUntil('\n');
          delay(2000);
          input.trim();
          if (input.equalsIgnoreCase("VENT_ON")) {
            forzarVentilacion = true;
            ventilacionActiva = true;
            device.showDisplay("V:MAN ON", 0, 15);
          } else if (input.equalsIgnoreCase("VENT_OFF")) {
            forzarVentilacion = true;
            ventilacionActiva = false;
            device.showDisplay("V:MAN OFF", 0, 15);
          } else if (input.equalsIgnoreCase("RIEGO_ON")) {
            forzarRiego = true;
            riegoActivo = true;
            device.showDisplay("R:MAN ON", 0, 30);
          } else if (input.equalsIgnoreCase("RIEGO_OFF")) {
            forzarRiego = true;
            riegoActivo = false;
            device.showDisplay("R:MAN OFF", 0, 30);
          } else if (input.equalsIgnoreCase("AUTO")) {
            forzarRiego = false;
            forzarVentilacion = false;
            device.showDisplay("MODO AUTO", 0, 45);
          }
          delay(1000);
        }

        if (!digitalRead(ENC_PUSH)) {
          delay(300);
          menu = true;
          device.clear();
          device.mostrarMenu(cont);
        }
        break;
    }
  }
}