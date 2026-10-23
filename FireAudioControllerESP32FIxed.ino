#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <ArduinoJson.h>
#include "BluetoothSerial.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
#define I2C_SDA 21
#define I2C_SCL 22

#define PREV_BUT 19
#define PAUSE_BUT 18
#define NEXT_BUT 5
#define MUTE_BUT 17

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
BluetoothSerial ESP_BT;
char source[64] = "Connection:";
char name[64] = "Null";
int scrollX = 8;

unsigned long lastPressedTime = 0;
const unsigned long COOLDOWN_DELAY = 1000;
unsigned int cursorInSeconds = 0;
unsigned int endInSeconds = 0;

struct {
  bool isPause : 1;
  bool isPressedNext : 1;
  bool isPressedPrev : 1;
} status = {true, false, false};

char bufferCursor[9]; 
char bufferEnd[9];
char serialBuffer[256]; 
size_t serialIndex = 0;

void workWithInfo();
void parseJson(char* jsonStr);
void updateDisplay();
void formatToTime(unsigned int secs, char* buffer, size_t bufferSize);
void drawTime();
void drawName();
void drawControl();
void initPict();

void setup() {
  Serial.begin(115200, SERIAL_8N1, -1, -1, false, 20000, 512); 
  ESP_BT.begin("Media Controller"); 
  ESP_BT.setTimeout(150); 
  
  pinMode(PREV_BUT,INPUT_PULLDOWN);
  pinMode(PAUSE_BUT,INPUT_PULLDOWN);
  pinMode(NEXT_BUT,INPUT_PULLDOWN);
  pinMode(MUTE_BUT,INPUT_PULLDOWN);
  lastPressedTime=millis();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for (;;);
  }
  u8g2Fonts.begin(display); 
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFontDirection(0);
  
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  
  updateDisplay(); 
}

void loop() {
  workWithInfo();
  controlButtons();
  static unsigned long lastScrollTime = 0;
  if (millis() - lastScrollTime > 150) {
    lastScrollTime = millis();
    if (!status.isPause) {
      scrollX -= 2;
      int textWidth = u8g2Fonts.getUTF8Width(name);
      if (scrollX < -textWidth) {
        scrollX = 120; 
      }
      updateDisplay();
    }
  }
}

bool checkButtons() {
    if (digitalRead(PREV_BUT))  { ESP_BT.println(F("PREV"));  return true; }
    if (digitalRead(PAUSE_BUT)) { ESP_BT.println(F("PAUSEPLAY")); return true; }
    if (digitalRead(NEXT_BUT))  { ESP_BT.println(F("NEXT"));  return true; }
    if (digitalRead(MUTE_BUT))  { ESP_BT.println(F("MUTE"));  return true; }
    return false;
}

void controlButtons(){
  if (millis()-lastPressedTime>COOLDOWN_DELAY)
  {
    if (checkButtons()){lastPressedTime=millis();}
  }
}

void updateDisplay() {
  display.clearDisplay();
  initPict();
  drawName();
  drawTime();
  drawControl();
  display.display();
}

void workWithInfo() {
  while (ESP_BT.available()) {
    char c = ESP_BT.read();
    
    if (c == '\n') {
      serialBuffer[serialIndex] = '\0';
      parseJson(serialBuffer);
      serialIndex = 0;
    } 
    else if (serialIndex < sizeof(serialBuffer) - 1) {serialBuffer[serialIndex++] = c;}
  }
}

void parseJson(char* jsonStr) {
  StaticJsonDocument<512> doc; 
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
      ESP_BT.print(F("Ошибка JSON: "));
      ESP_BT.println(error.f_str());
      return;
  }
  if (doc.containsKey("source")) {strlcpy(source, doc["source"].as<const char*>(), sizeof(source));}
  
  if (doc.containsKey("title")) {strlcpy(name, doc["title"].as<const char*>(), sizeof(name));}

  if (doc.containsKey("status")) {
    const char* pStatus = doc["status"].as<const char*>();
    status.isPause = (strcmp(pStatus, "PLAYING") != 0); 
  }
  
  cursorInSeconds = doc["time_position"] | cursorInSeconds;
  endInSeconds = doc["time_duration"] | endInSeconds;
  
  ESP_BT.println(F("Handshake received"));
  updateDisplay(); 
}

void drawControl(){
  u8g2Fonts.setForegroundColor(WHITE);
  if (status.isPause) {
    display.drawRect(59, 46, 3, 10, SSD1306_WHITE);
    display.drawRect(66, 46, 3, 10, SSD1306_WHITE);
  } 
  else {
    display.drawTriangle(62, 55, 67, 51, 62, 46, SSD1306_WHITE);
  }
  if (!status.isPressedNext) {
    display.drawTriangle(73, 55, 78, 51, 73, 46, SSD1306_WHITE);
    display.drawRect(80, 46, 2, 10, SSD1306_WHITE);
  } 
  else {
    display.fillTriangle(77, 55, 78, 51, 73, 46, SSD1306_WHITE);
    display.fillRect(80, 46, 2, 10, SSD1306_WHITE);
  }
  if (!status.isPressedPrev) {
    display.drawTriangle(54, 55, 49, 51, 54, 46, SSD1306_WHITE);
    display.drawRect(46, 46, 2, 10, SSD1306_WHITE);
  } 
  else {
    display.fillTriangle(54, 55, 49, 51, 54, 46, SSD1306_WHITE);
    display.fillRect(46, 46, 2, 10, SSD1306_WHITE);    
  }
}

void drawTime() {
  u8g2Fonts.setForegroundColor(WHITE);
  formatToTime(cursorInSeconds, bufferCursor, sizeof(bufferCursor));
  formatToTime(endInSeconds, bufferEnd, sizeof(bufferEnd));

  u8g2Fonts.setCursor(2, 55);
  u8g2Fonts.print(bufferCursor);
  u8g2Fonts.setCursor(82, 55); 
  u8g2Fonts.print(bufferEnd);

  if (endInSeconds > 0) {
    unsigned long percentX = (111 - 16) * (unsigned long)cursorInSeconds / endInSeconds;
    if (percentX > (111 - 16)) percentX = (111 - 16);
    
    unsigned int cordX = 16 + percentX;
    display.drawRect(cordX - 1, 38, 3, 9, SSD1306_WHITE);
  }
}

void drawName() {
  u8g2Fonts.setForegroundColor(WHITE);
  u8g2Fonts.setCursor(8, 15);
  u8g2Fonts.print(source);
  u8g2Fonts.setCursor(scrollX, 30); 
  u8g2Fonts.print(name);
}

void formatToTime(unsigned int secs, char* buffer, size_t bufferSize) {
  unsigned int hours = secs / 3600;
  unsigned int minutes = (secs % 3600) / 60;
  unsigned int seconds = secs % 60;
  
  snprintf(buffer, bufferSize, "%u:%02u:%02u", hours, minutes, seconds);
}

void initPict() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawLine(16, 42, 111, 42, SSD1306_WHITE);
}
