#ifndef DEVICE
#define DEVICE
#include <Arduino.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>
#include <Wire.h>




class Device {
    public:
        Device(int w, int h, int reset, int pinDHT, int modelo);
        void begin();
        void showDisplay(String txt, int x, int y);
        void dibujarPixel(int x, int y);
        void dibujarSol();
        void dibujarCheck();
        void dibujarGota(float h);
        float readTemp();
        float readHum(); 
        void clear();


    private:
        Adafruit_SH1106G _display;
        DHT _sensor;
};


#endif