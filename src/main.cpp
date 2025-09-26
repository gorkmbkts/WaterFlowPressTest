#include <Arduino.h>
#include "FlowSensor.h"
#include "LevelSensor.h"
#include "Buttons.h"
#include "Joystick.h"
#include "LcdUI.h"
#include "SdLogger.h"
#include <Wire.h>

// ---- Pins ----
static const int PIN_BTN1 = 13;
static const int PIN_BTN2 = 14;
static const int PIN_JOY_X = 26;
static const int PIN_JOY_Y = 27;
static const int PIN_LCD_SDA = 21;
static const int PIN_LCD_SCL = 22;

static const int PIN_FLOW = 25;
static const int PIN_LEVEL_ADC = 32;

static const int PIN_SD_CS = 5;
static const int PIN_SD_MOSI = 23;
static const int PIN_SD_MISO = 19;
static const int PIN_SD_SCK = 18;



void setup() {
  Serial.begin(9600);
}

void loop() {
  
}
