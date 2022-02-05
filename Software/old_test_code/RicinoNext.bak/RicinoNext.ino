#include <ESPAsync_WiFiManager.h>

#include <ArduinoJson.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
//#include <ESPAsyncWiFiManager.h>
#include <AsyncElegantOTA.h>;

#include <FS.h>

/* AsyncWebServer Stuff */
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include "Button2.h"

TFT_eSPI tft = TFT_eSPI(135, 240);  // Invoke library, pins defined in User_Setup.h

#define TFT_GREY 0x5AEB // New colour

#define BUTTON_1            35
#define BUTTON_2            0

Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

void button_init()
{
    btn1.setDebounceTime(50);
    btn2.setDebounceTime(50);
    
    btn1.setTapHandler([](Button2& btn) {
        Serial.println("A clicked");
        static bool state = false;
        state = !state;
        tft.fillRect(0, 100, 120, 35, state ? TFT_WHITE : TFT_BLACK);
//        //After using light sleep, you need to disable timer wake, because here use external IO port to wake up
//        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
//        // esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
//        esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
//        delay(200);
//        esp_deep_sleep_start();
    });

    btn2.setTapHandler([](Button2& btn) {
        static bool state = false;
        state = !state;
        Serial.println("B clicked");
        tft.fillRect(120, 100, 120, 35, state ? TFT_WHITE : TFT_BLACK);
    });
}



void setup(void) {
  Serial.begin(9600);

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  ESPAsync_WiFiManager ESPAsync_wifiManager(&server, &dns, "RicinoNextAP");
  //ESPAsync_wifiManager.resetSettings();   //reset saved settings
  ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  ESPAsync_wifiManager.autoConnect("RicinoNextAP");
//
//  if (WiFi.status() == WL_CONNECTED) { Serial.print(F("Connected. Local IP: ")); Serial.println(WiFi.localIP()); }
//  else { Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status())); }
  // Route to index.html + favicon.ico
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });

  AsyncElegantOTA.begin(&server);
//  server.on("/",HTTP_GET, handleRoot);
  server.begin();

//  ws.onEvent(onWSEvent);
//  server.addHandler(&ws);


  tft.init();
  tft.setRotation(1);

  button_init();
  
  for (uint8_t i = 5; i != 0; i--){
      tft.setCursor(120 - 15, 70 - 25);
      tft.setTextSize(5);
      tft.setTextColor(TFT_RED);
      tft.fillScreen(TFT_BLACK);
      tft.print(i);
      delay(1000);
  }
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0,10);
  tft.setTextColor(TFT_RED);
  tft.print("Counter: ");
  delay(100);
  tft.setCursor(200,10);
  tft.setTextColor(TFT_BLUE);
  tft.print("CYCLES");
}

void loop() {
  static uint32_t cleanerTimer = millis();
  const uint32_t cleanerDelay = 100;
  static uint32_t milliTimer = millis();
  const uint32_t milliDelay = 1;
  static uint32_t blinkTimer = millis();
  const uint32_t blinkDelay = 1000;
  static uint32_t counter = 0;
  static bool blinkState = false;
  btn1.loop();
  btn2.loop();
    
  if (millis() - cleanerTimer > cleanerDelay){
    tft.fillRect(70, 10, 110, 15, TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(70,10);
    tft.print(counter);
    cleanerTimer = millis();
  }
  if (millis() - milliTimer > milliDelay){
    milliTimer = millis();
    counter++;
  }
  if (millis() - blinkTimer > blinkDelay){
    blinkTimer = millis();
    blinkState = !blinkState;
    tft.fillRect(0, 0, 240, 1, blinkState ? TFT_RED : TFT_BLACK);
  }

}
