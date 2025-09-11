#include <Device.h>


const byte LED = 23;
const byte ENC_PUSH = 19;
const byte POT = 32;
volatile bool pantalla = true;
Device device = Device(128, 64, -1, 33, DHT22);
volatile float hum_min = random(40, 60);

void setup() {
  
  Serial.begin(9600);
  device.begin();
  pinMode(LED, OUTPUT);
  pinMode(ENC_PUSH, INPUT_PULLUP);
  
  char c_hum[40];
  sprintf(c_hum, "Humedad mínima: %.2f %", hum_min);
  Serial.println(c_hum);
  device.showDisplay("Sistema iniciando...", 0, 15);
  delay(3000);
}

void loop() {
  float t_ref = analogRead(POT);
  t_ref = map(t_ref, 0, 4095, 20.0, 40.0);
  float hum = device.readHum();
  float t = device.readTemp();
  

  if (!digitalRead(ENC_PUSH)) {
    pantalla = !pantalla;
  }
  
  device.clear();
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
    delay(200);
  
  

  //Falta hacer útil el encoder y hacer un menú de opciones con eso

  
}
