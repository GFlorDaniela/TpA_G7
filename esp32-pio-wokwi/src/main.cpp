#include <Device.h>


const byte LED = 23;
const byte BOTON = 2;
const byte POT = 32;
volatile bool pantalla = true;
Device device = Device(128, 64, -1, 14, DHT22);

void setup() {
  
  Serial.begin(9600);
  device.begin();
  pinMode(LED, OUTPUT);
  pinMode(BOTON, INPUT_PULLUP);
}

void loop() {
  float valor = analogRead(POT);
  valor = map(valor, 0, 4095, 14.0, 40.0);
  float hum = device.readHum();
  float t = device.readTemp();
  Serial.println(valor);
  if (!digitalRead(BOTON)) {
    pantalla = !pantalla;
  }
  
  device.clear();
  if (pantalla) {
    
    if (t >= valor) {
    
      device.showDisplay("Temperatura", 0, 15);
      device.dibujarSol();
      digitalWrite(LED, HIGH);
    }
    else  {
      
      device.showDisplay("Temperatura", 0, 15);
      device.dibujarCheck();
      digitalWrite(LED, LOW);
    }
    }
  else {
    
    device.showDisplay("Humedad", 0, 15);
    device.dibujarGota(hum);
    Serial.println(hum);
    if (hum < 45) {
      
      digitalWrite(LED, HIGH);
      delay(400);
      digitalWrite(LED, LOW);
      
      
    }
    
  }
    delay(400);
  
  

  

  
}
