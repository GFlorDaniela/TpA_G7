#include <Device.h>

const byte LED = 23;
const byte ENC_PUSH = 19;
const byte POT = 32;
const byte ENC_CLK = 18;
const byte ENC_DT = 5;
bool pantalla = true; // controlan si no estas en el menú, si es true corresponde a temperatura, sino a humedad.
bool menu = true;
Device device = Device(128, 64, -1, 33, DHT22);
volatile float hum_min = random(40, 60); // valor de referencia (mínimo de humedad).
volatile int cont = 0;
int pasar = 0;
float t_ref;
bool forzarVentilacion = false; // permiten activar manualmente los sistemas.
bool forzarRiego = false;

bool ventilacionActiva = false; // representan si los sistemas están realmente funcionando.
bool riegoActivo = false;

// Variables que guardan el último estado para detectar cambios
bool prevVent = false;   // último estado conocido de ventilación
bool prevRiego = false;  // último estado conocido de riego

bool t_forz = false;
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 1000;

// Variables para mensajes al cambiar referencias
String mensajeSerial = "";       // Variable global para el mensaje
bool mostrarMensaje = false;     // Indica si se debe mostrar en pantalla
unsigned long mensajeTiempo = 0; // Guarda el momento en que se actualizó el mensaje


// Variables para interrupciones
volatile int lastCLKState;
volatile bool encoderChanged = false;
unsigned long lastEncoderTime = 0;
// encoderDebounce es el tiempo mínimo entre giros válidos (por ejemplo 150 ms). DUDA: esta muy sensible, podriamos subirle el tiempo.
const unsigned long encoderDebounce = 150;

// Función de interrupción para el encoder - SOLO en menú principal
// Detecta giros en el encoder solo cuando estás en el menú principal.
// Incrementa/decrementa cont (índice del menú entre 0 y 3).

// IRAM_ATTR → la función se guarda en la RAM interna (no en flash), porque debe ejecutarse muy rápido
void IRAM_ATTR handleEncoder() {
  if (menu) {  // ⭐⭐ Solo funciona en menú principal ⭐⭐

    // Así evitamos que el ruido mecánico del encoder cause múltiples incrementos por error
    if (millis() - lastEncoderTime > encoderDebounce) {
      // Leemos las dos dos señales digitales que tiene el encoder, CLK y DT
      int clkValue = digitalRead(ENC_CLK);
      int dtValue = digitalRead(ENC_DT);
      
      // Si clkValue es distinto a lastCLKState quiere decir que el encoder giró
      if (clkValue != lastCLKState) {
        // Si dt es distinto de clk, se movió a la derecha
        if (dtValue != clkValue) {
          cont++;
        } else {
          // Si son iguales, se movió a la izquierda
          cont--;
        }
        // Mantener contador entre 0-3, que son las opciones del menú
        if (cont > 3) cont = 0;
        if (cont < 0) cont = 3;
        
        // Registramos que se cambió, para que en el loop se muestre visualmente en la pantalla
        encoderChanged = true;
        // lastEncoderTime guarda la última vez que se procesó un giro.
        lastEncoderTime = millis();
      }
      // Registramos el ultimo cambio del clk
      lastCLKState = clkValue;
    }
  }
}

void setup() {
  // Inicializamos el serial a 9600 baudios
  Serial.begin(9600);
  // Inicializamos los dispositivos que maneja la libreria device: sensor DHT, pantalla OLED (Adafruit_SH1106G).
  device.begin();
  // LED → salida digital para encender/apagar según el estado del riego o ventilación.
  pinMode(LED, OUTPUT);
  // ENC_PUSH → botón del encoder, configurado con INPUT_PULLUP para leer LOW cuando se presiona.
  pinMode(ENC_PUSH, INPUT_PULLUP);
  // ENC_CLK y ENC_DT → pines del encoder rotatorio, también con INPUT_PULLUP.
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);

  //Temperatura de referencia inicial
  t_ref = analogRead(POT);
  t_ref = map(t_ref, 0, 4095, 20.0, 40.0);

  // Configurar interrupciones
  // Inicializamos el ultimo valor del CKL como el valor actual del CKL
  lastCLKState = digitalRead(ENC_CLK);

  // Se llama a la función handleEncoder() cada vez que CLK cambia.
  // Permite que el menú se actualice de manera instantánea sin bloquear el loop().
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), handleEncoder, CHANGE);
  
  char c_hum[40];
  sprintf(c_hum, "Humedad mínima: %.2f %", hum_min);
  // Evento: umbral min humedad generado aleatoriamente
  Serial.println(c_hum);

  //Mostramos cartel de sistema iniciando por pantalla
  device.showDisplay("Sistema iniciando", 0, 15);
  delay(500);
  device.dibujarPixel(106,20);
  delay(500);
  device.dibujarPixel(110,20);
  delay(500);
  device.dibujarPixel(114,20);
  delay(500);
  device.clear();
  device.mostrarMenu(cont);
}

void loop() {
 
  // Leemos la humedad y la temperatura de los dispositivos con la lib device
  float hum = device.readHum();
  float t = device.readTemp();

  // ⭐⭐ Solo actualizar menú si estamos en menú principal ⭐⭐
  // Si estamos en el menú y giraron el encoder entonces tenemos que hacer que el menú se muestre con ">>" en la opción seleccionada
  if (encoderChanged && menu) {
    device.clear();
    device.mostrarMenu(cont);
    // encoderChanged se cambia a true en el manejo del encoder
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
    // Si no estamos en el menú y no estan forzados los sistemas, se asignan XActivo según los sensores y los valores de referencia
    if (!forzarVentilacion) {
      ventilacionActiva = (t >= t_ref);
    }
    if (!forzarRiego) {
      riegoActivo = (hum < hum_min);
    }

     // ======= REPORTAR CAMBIOS AUTOMÁTICOS =======

    // Si no estan forzados los sistemas entonces puede haber habido cambios que reportar como Eventos por serial
    // Si la ventilación cambió de estado lo reportamos por serial
    if (!forzarVentilacion && ventilacionActiva != prevVent) {
        Serial.println(String("Ventilacion: ") + (ventilacionActiva ? "ON" : "OFF"));
        prevVent = ventilacionActiva;
    }
    // Si el riego cambió de estado lo reportamos por serial
    if (!forzarRiego && riegoActivo != prevRiego) {
        Serial.println(String("Riego: ") + (riegoActivo ? "ON" : "OFF"));
        prevRiego = riegoActivo;
    }

    delay(200);

    // Si no estamos en el menú, mostramos la opcion de la variable cont
    // cont lo actualizamos en la función que maneja el encoder, segun dt y ckl
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
    // Si la t ref no fue cambiada por serial, se lee el potenciometro y se cambia al valor de ese momento
  if (!t_forz) {
    t_ref = analogRead(POT);
    t_ref = map(t_ref, 0, 4095, 20.0, 40.0);
  }
    if (pantalla) {
      // MODO TEMPERATURA
      // Muestra la temperatura actual t y la referencia t_ref
      char texto[64];
      sprintf(texto, "Temp: %.1f C", t);
      device.showDisplay(texto, 0, 10);
      device.showDisplay("Ref: " + String(t_ref, 1) + " C", 0, 25);
      
      // Si la ventilación está activa → dibuja un sol y pone Vent: ON.
      if (ventilacionActiva) {
        device.dibujarSol();
        device.showDisplay("Vent: ON", 0, 45);
      } else {
        // Si no está activa → dibuja un check y pone Vent: OFF.
        device.dibujarCheck();
        device.showDisplay("Vent: OFF", 0, 45);
      }
    } else {
      // MODO HUMEDAD
      // Muestra humedad actual hum y mínima de referencia hum_min.
      char texto[64];
      sprintf(texto, "Hum: %.1f %%", hum);
      device.showDisplay(texto, 0, 10);

      // Dibuja una gota proporcional a la humedad.
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
  // Si estamos en humedad y el riego está activo entonces:
  if (riegoActivo && !pantalla) {
    // Parpadeo para riego activo, cada 500ms

    // millis() devuelve el tiempo en milisegundos desde que se encendió el ESP32.
    // lastLedChange guarda el momento en que la LED cambió de estado por última vez.
    // Esta condición pregunta: ¿Pasaron más de 500 ms desde el último cambio de estado del LED?
    // Si sí → toca cambiar el estado otra vez.
    // Si no → no se hace nada (se espera).
    if (millis() - lastLedChange > 500) {
      // Actualizas el estado del ultimo cambio del LED
      lastLedChange = millis();
      // Invierte el estado de la variable booleana ledState.
      ledState = !ledState;
      // Actualiza el estado del LED
      digitalWrite(LED, ledState ? HIGH : LOW);
    }
  } 
  // Si estamos en temperatura y la ventilación está activa entonces:
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
    delay(300); // Debounce corto DUDA: lo cambiamos para menos sensibilidad?
    pantalla = !pantalla; // Cambiamos de temperatura a humedad
    pasar++; // Incrementamos pasar que toma valor 0 en temp, 1 en hum y si llega a 2 vuelve al menú
    
    // Forzar refresco inmediato
    lastRefresh = 0;
    device.clear();
    
    // Si pasar llega a 2 volvemos al menú
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
          // Si la temperatura de referencia no fue cambaida por serial, 
          // entonces se lee y asigna el valor del potenciometro en ese momento 
          // DUDA:esto esta bien?
           if (!t_forz) {
            t_ref = analogRead(POT);
            t_ref = map(t_ref, 0, 4095, 20.0, 40.0);
          }
          // ⭐⭐ MEJOR VISUALIZACIÓN INFO COMPLETA ⭐⭐
          // Mostramos el valor de los sensores y los valroes de referencia
          device.showDisplay("INFO COMPLETA", 0, 0);
          device.showDisplay("T:" + String(t, 1) + "C R:" + String(t_ref, 1) + "C", 0, 12);
          device.showDisplay("H:" + String(hum, 1) + "% M:" + String(hum_min, 1) + "%", 0, 24);
          
          // Estado sistemas más compacto
          // La LED parpadea en un patrón (ON → OFF → ON rápido) para indicar que 
          // los dos sistemas están funcionando al mismo tiempo.
          if (ventilacionActiva & riegoActivo) {
          device.showDisplay("Vent ON", 0, 52);
          device.showDisplay("Riego ON", 64, 52);
          digitalWrite(LED, HIGH);
          delay(500);
          digitalWrite(LED, LOW);
          delay(150);
          digitalWrite(LED, HIGH);
          
        }
        // Si solo esta activa la ventilación 
        // Se mantiene prendida la LED
        else if (ventilacionActiva) {
          device.showDisplay("Vent ON", 0, 52);
          device.showDisplay("Riego OFF", 64, 52);
          digitalWrite(LED, HIGH);
        }
        // Si solo el riego está activo 
        // La LED parpadea con un patrón distinto (ON → OFF más lento).
        else if (riegoActivo) {
          device.showDisplay("Vent OFF", 0, 52);
          device.showDisplay("Riego ON", 64, 52); 
          digitalWrite(LED, HIGH);
          delay(400);
          digitalWrite(LED, LOW);
          delay(200);
        }
        // Si nungun sistema esta activo, la led no se enciende
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
        
        // Si hay algo en el serial lo leemos
        // Si empieza con T= configuramos la t_ref a ese valor, y con t_forz=true ya no se puede configurar la temp desde el potenciometro
        // Si empieza con H= configuramos la hum_min a ese valor
        // Mostramos los nuevos valores
        // Si es POT activamos nuevamente que se pueda variar la temperatura por el potenciometro
        if (Serial.available()) {
          delay(2000);
          String input = Serial.readString();
          input.trim();
          if (input.startsWith("T=")) {
            t_forz = true;
            t_ref = input.substring(2).toFloat();
            mensajeSerial = "Nueva T: " + String(t_ref,1);
            //device.showDisplay(mensajeSerial, 0, 48);
            // Cambio de Referencia
            Serial.println(mensajeSerial);
            mostrarMensaje = true;
            mensajeTiempo = millis();
          } else if (input.startsWith("H=")) {
            hum_min = input.substring(2).toFloat();
            //device.showDisplay("Nueva H: " + String(hum_min,1), 0, 48);
            mensajeSerial = "Nueva H: " + String(hum_min,1);
            Serial.println(mensajeSerial);
            mostrarMensaje = true;
            mensajeTiempo = millis();
          }
          else if (input.startsWith("POT")) {
            t_forz = false;
            //device.showDisplay("Usando POT", 0, 48);
            mensajeSerial = "Usando POT";
            Serial.println(mensajeSerial);
            mostrarMensaje = true;
            mensajeTiempo = millis();
          }
          // delay(1000);
        }

         // Mostrar mensaje si corresponde
        if (mostrarMensaje) {
          device.showDisplay(mensajeSerial, 0, 48);
          if (millis() - mensajeTiempo > 2000) { // 2 segundos
            mostrarMensaje = false;
          }
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
        // En este menú el usuario puede forzar manualmente la ventilación o el riego
        // mediante comandos enviados por Serial (VENT_ON, VENT_OFF, RIEGO_ON, RIEGO_OFF, AUTO).
        // Con X_ON o X_OFF se fuerza el sistema a ese valor. Con AUTO se deja en manual, segun los sensores y valor de ref.
        device.clear();
        device.showDisplay("FORZAR SISTEMAS", 0, 0);
        // Mostramos V: ON o V:OFF si esta forzado el sistema. Sino Mostramos V:AUTO
        device.showDisplay("V:" + String(forzarVentilacion ? (ventilacionActiva ? "ON" : "OFF") : "AUTO"), 0, 15);
        // Mostramos R: ON o R:OFF si esta forzado el sistema. Sino Mostramos R:AUTO
        device.showDisplay("R:" + String(forzarRiego ? (riegoActivo ? "ON" : "OFF") : "AUTO"), 64, 15);
        // Mostramos las opciones que tiene para ingresar por serial
        device.showDisplay("VENT_ON", 0, 45);
        device.showDisplay("VENT_OFF", 0, 55);
        device.showDisplay("RIEGO_ON", 64, 45);
        device.showDisplay("RIEGO_OFF", 64, 55);
        device.showDisplay("AUTO", 40, 30);

        // Si hay algo en el serial, se lo lee
        // Si es AUTO se pone el forzarX en false. Fuera del menú se actualizan las variables XActivo
        // Si es X_ON o X_OFF se prende forzarX y se pone en true o false XActiva correspondientemente
        if (Serial.available()) {
          String input = Serial.readStringUntil('\n');
          delay(2000);
          input.trim();
          if (input.equalsIgnoreCase("VENT_ON")) {
            forzarVentilacion = true;
            ventilacionActiva = true;
            device.showDisplay("V:MAN ON", 0, 15);
            Serial.println("Ventilacion: ON (forzada)");
          } else if (input.equalsIgnoreCase("VENT_OFF")) {
            forzarVentilacion = true;
            ventilacionActiva = false;
            device.showDisplay("V:MAN OFF", 0, 15);
            Serial.println("Ventilacion: OFF (forzada)");
          } else if (input.equalsIgnoreCase("RIEGO_ON")) {
            forzarRiego = true;
            riegoActivo = true;
            device.showDisplay("R:MAN ON", 0, 30);
            Serial.println("Riego: ON (forzado)");
          } else if (input.equalsIgnoreCase("RIEGO_OFF")) {
            forzarRiego = true;
            riegoActivo = false;
            device.showDisplay("R:MAN OFF", 0, 30);
            Serial.println("Riego: OFF (forzada)");
          } else if (input.equalsIgnoreCase("AUTO")) {
            forzarRiego = false;
            forzarVentilacion = false;
            device.showDisplay("MODO AUTO", 0, 45);
          }
          delay(1000);
        }

        // ENC_PUSH es el pin donde está conectado el pulsador del encoder (cuando lo presionás).
        // digitalRead(ENC_PUSH) devuelve
        //   - HIGH → si el botón no está presionado
        //   - LOW → si el botón sí está presionado.
        // Entonces si !digitalRead es true, se clickeo el boton para volver al menu=true
        if (!digitalRead(ENC_PUSH)) {
          // Para evitar el rebote
          delay(300);
          menu = true;
          device.clear();
          device.mostrarMenu(cont);
        }
        break;
    }
  }
}
