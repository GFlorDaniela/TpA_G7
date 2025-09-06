#ifndef _DEVICE
#define _DEVICE

#include <Arduino.h>
//dependencies:
#include <Adafruit_SH110X.h>
#include <Wire.h> 
#include <DHT.h>

class Device{   
  public:
    Device(int w, int h, int reset, int pinOLED,  int model);
    void begin();
    void showDisplay(const char * text);
    float readTemp();
    float readHum();
    
  private:
    Adafruit_SH1106G _display;
    DHT _sensor; 
};

#endif