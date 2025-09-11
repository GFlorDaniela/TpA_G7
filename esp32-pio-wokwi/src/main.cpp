#include <Device.h>


const byte LED = 23;
const byte ENC_PUSH = 19;
const byte POT = 32;
volatile bool pantalla = true;
Device device = Device(128, 64, -1, 33, DHT22);

void setup() {
  
  Serial.begin(9600);
  device.begin();
  pinMode(LED, OUTPUT);
  pinMode(ENC_PUSH, INPUT_PULLUP);
}

void loop() {
  float valor = analogRead(POT);
  valor = map(valor, 0, 4095, 14.0, 40.0);
  float hum = device.readHum();
  float t = device.readTemp();

  if (!digitalRead(ENC_PUSH)) {
    pantalla = !pantalla;
  }
  
  device.clear();
  if (pantalla) {
    char texto[64];
    sprintf(texto, "Temp: %.2f C", t);
    if (t >= valor) {
      
      device.showDisplay(texto, 0, 15);
      device.dibujarSol();
      digitalWrite(LED, HIGH);
    }
    else  {
      
      device.showDisplay(texto, 0, 15);
      device.dibujarCheck();
      digitalWrite(LED, LOW);
    }
    }
  else {
    char texto[64];
    sprintf(texto, "Hum: %.2f %", hum);
    device.showDisplay(texto, 0, 15);
    device.dibujarGota(hum);
    if (hum < 45) {
      
      digitalWrite(LED, HIGH);
      delay(400);
      digitalWrite(LED, LOW);
      
      
    }
    
  }
    delay(400);
  
  

  

  
}
