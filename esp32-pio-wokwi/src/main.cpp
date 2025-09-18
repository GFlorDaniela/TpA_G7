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
int cont = 0;
int actualCLK;
int pasar = 0;

bool forzarVentilacion = false;
bool forzarRiego = false;

bool ventilacionActiva = false;
bool riegoActivo = false;
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 2000; // actualizar cada 2 segundo



void setup() {
  
  Serial.begin(9600);
  device.begin();
  pinMode(LED, OUTPUT);
  pinMode(ENC_PUSH, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);

  

  
  char c_hum[40];
  sprintf(c_hum, "Humedad mínima: %.2f %", hum_min);
  Serial.println(c_hum);
  device.showDisplay("Sistema iniciando...", 0, 15);
  delay(2000);
  device.clear();
  device.mostrarMenu(cont);
}

void loop() {
  float t_ref = analogRead(POT);
  t_ref = map(t_ref, 0, 4095, 20.0, 40.0);
  float hum = device.readHum();
  float t = device.readTemp();

  actualCLK = digitalRead(ENC_CLK);
  if (!actualCLK) {
    device.clear();
    if (digitalRead(ENC_DT) != actualCLK) {
      cont += 1;
    }
    else {
      cont -= 1;
    }

    if (cont > 3) {
      cont = 0;
    }
    if (cont < 0) {
      cont = 3;
    }
  }
  delay(200);
  
  if (menu) {
    device.mostrarMenu(cont);
    if (!digitalRead(ENC_PUSH)) {
      menu = !menu;

    }
  } else {
    delay(200);
    ventilacionActiva = (t >= t_ref);
    riegoActivo = (hum < hum_min);
    device.clear();
    

    switch (cont) {
      case 0:
        
        if (pantalla) {
          char texto[64];
          sprintf(texto, "Temp: %.2f C", t);
          device.showDisplay(texto, 0, 15);
          device.showDisplay("T referencia: " + String(t_ref) + " C", 0, 36);
          if (t >= t_ref) {
            
            device.dibujarSol();
            device.showDisplay("Ventilacion Activa", 0, 54);
            digitalWrite(LED, HIGH);
          }
          else  {
            
            device.dibujarCheck();
            device.showDisplay("Ventilacion No activa", 0, 54);
            digitalWrite(LED, LOW);
          }
        }
        else {
          char texto[64];
          sprintf(texto, "Hum: %.2f %", hum);
          device.showDisplay(texto, 0, 15);
          device.dibujarGota(hum);
        
          device.showDisplay("Hum minima: " + String(hum_min) + " %", 0, 36);
          if (hum < hum_min) {
            device.showDisplay("Riego Activado", 0, 54);
            digitalWrite(LED, HIGH);
            delay(400);
            digitalWrite(LED, LOW);
            delay(200);
            
          }
          else {
            device.showDisplay("Riego Desactivado", 0, 54);
          }
          
        }
        if (!digitalRead(ENC_PUSH)) {
          pantalla = !pantalla;
          pasar += 1;
          if (pasar == 2) {
            pasar = 0;
            menu = !menu;
            device.clear();
          }
        }
        break;
      case 1:
        if (millis() - lastUpdate > updateInterval) {
        lastUpdate = millis();
        device.clear();
        device.showDisplay("Info Completa:", 0, 0);
        device.showDisplay("Temp: " + String(t, 1) + " C", 0, 12);
        device.showDisplay("T ref: " + String(t_ref, 1) + " C", 0, 22);
        device.showDisplay("Hum: " + String(hum, 1) + " %", 0, 32);
        device.showDisplay("H min: " + String(hum_min, 1) + " %", 0, 42);

        // Mostrar estado de sistemas
        if (ventilacionActiva) {
          device.showDisplay("Vent ON", 0, 52);
        } else {
          device.showDisplay("Vent OFF", 0, 52);
        }

        if (riegoActivo) {
          device.showDisplay("Riego ON", 64, 52);
        } else {
          device.showDisplay("Riego OFF", 64, 52);
        }
      }

      if (!digitalRead(ENC_PUSH)) {
        menu = !menu;
        device.clear();
      }
      break;
      case 2:
        device.showDisplay("Cambio por serial", 0, 15);
        if (Serial.available()) {
          String input = Serial.readStringUntil('\n');
          input.trim();
          if (input.startsWith("T=")) {
            t_ref = input.substring(2).toFloat();
            Serial.println("Nueva T ref: " + String(t_ref));
          } else if (input.startsWith("H=")) {
            hum_min = input.substring(2).toFloat();
            Serial.println("Nueva H min: " + String(hum_min));
          }
        }
        if (!digitalRead(ENC_PUSH)) {
          menu = !menu;
          device.clear();
        }
        break;
      case 3:
        device.showDisplay("Forzar sistemas", 0, 0);
        device.showDisplay(forzarVentilacion ? "Vent ON" : "Vent OFF", 0, 20);
        device.showDisplay(forzarRiego ? "Riego ON" : "Riego OFF", 0, 36);

        if (Serial.available()) {
          String input = Serial.readStringUntil('\n');
          input.trim();
          if (input.equalsIgnoreCase("VENT_ON")) {
            forzarVentilacion = true;
          } else if (input.equalsIgnoreCase("VENT_OFF")) {
            forzarVentilacion = false;
          } else if (input.equalsIgnoreCase("RIEGO_ON")) {
            forzarRiego = true;
          } else if (input.equalsIgnoreCase("RIEGO_OFF")) {
            forzarRiego = false;
          }
        }

        // acción física de los sistemas
        if (forzarVentilacion) digitalWrite(LED, HIGH);
        if (forzarRiego) {
          digitalWrite(LED, HIGH);
          delay(400);
          digitalWrite(LED, LOW);
          delay(200);
        }

        if (!digitalRead(ENC_PUSH)) {
          menu = !menu;
          device.clear();
        }
        break;
      default:
          Serial.println("Error en opcion");
          menu = !menu;
          break;
    }
    
        
    
  }
   
}
