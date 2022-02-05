#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsync_WiFiManager.h>
#include <ESPAsyncWebServer.h>
//#include <AsyncElegantOTA.h>;

#include <FS.h>

#include <ArduinoJson.h>

#define DECODE_NEC
#include <IRremote.h>

/* AsyncWebServer Stuff */
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include "Button2.h"

TFT_eSPI tft = TFT_eSPI(135, 240);  // Invoke library, pins defined in User_Setup.h

#define DO_NOT_USE_FEEDBACK_LED
#define IR_RECEIVE_PIN    15
#define MARK_EXCESS_MICROS    20

#define TFT_GREY 0x5AEB // New colour

#define CAR_IRCODE_START 0xAA
#define CAR_NUMBER 8
#define RED_PIN 4
#define GREEN_PIN 6

#define BUTTON_1            35
#define BUTTON_2            0

Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

// Prototype
void updateDataLoop();

/* =============== config section start =============== */
enum State {
  DISCONNECTED, // Disconnected, only allow connection
  CONNECTED, // connected ( start to test alive ping )
  SET,      // Set RACE init.
  START, // When START add 20s timeout for RACE
  RACE, // RACE, update dataRace()...
  END,  // RACE finish, wait 20sec all players
  RESET  // RACE finished auto/manual, reset every parameters
};

enum State raceStateENUM;

// Struct to keep Order of racerBuffer
struct rankOrder{
  uint8_t rank[CAR_NUMBER] = {0};
  // uint8_t prevRank[CAR_NUMBER] = {0};
  uint8_t laps[CAR_NUMBER] = {0}; // change to checkpoint for future?
  uint8_t prevLaps[CAR_NUMBER] = {0};
  uint8_t numberRegistered = 0;
};
struct rankOrder aRankOrder;
// memset(&aRankOrder, 0, sizeof(rankOrder));

// Struct to keep every Race laps time and some calculation.
struct racerMemory{
  public:
      uint16_t id; // uint32_t to accept robitronic/lapz etc..?
      uint8_t rank;
      uint8_t color; // 1 to 4 for the moment...
      uint8_t numberLap;
      uint32_t offsetLap;
      uint32_t lastLap;
      uint32_t bestLap;
      uint32_t meanLap;
      uint32_t totalLap;
      uint32_t _tmpLast;

      void reset(){
          id = 0;
          rank = 0;
          color = 0;
          numberLap = 0;
          offsetLap = 0;
          lastLap = 0;
          bestLap = 0;
          meanLap = 0;
          totalLap = 0;
          _tmpLast = 0;
      }

      void init(uint8_t id, uint32_t currentTime){
          this->id = id;
          offsetLap = currentTime; //it's a feature! not a bug. ie: start the real counter after the first detection?!?
          _tmpLast = currentTime;
      }
      
      void setCurrentLap(uint32_t currentTime){
          numberLap++;
          totalLap = currentTime - offsetLap;
          meanLap = totalLap / numberLap;
          lastLap = currentTime - _tmpLast;
          _tmpLast = currentTime;
          if (lastLap < bestLap || bestLap == 0){
              bestLap = lastLap;
          }
//          _state = true;
      }

      bool getState(uint8_t debounceSecond){
          uint8_t tmpSecond = (uint8_t)millis() / 1000;
          _isReady = (tmpSecond - _lastSecondView >= debounceSecond) ? true : false;
          _lastSecondView = tmpSecond;
          return _isReady;
      }
//      uint32_t getTimeLap(uint32_t _startRaceMillis){
//          uint32_t tmp = 0;
//          if (totalLap > 0){
//              tmp = millis() - _startRaceMillis - _tmpLast;
//          }
//          return tmp;
//      }
      
//      bool operator> (const racerBuffer &rankorder)
//      {
//          return (numberLap > rankorder.numberLap);
//          //((numberLap << 8) + meanLap) > ((rankorder.numberLap << 8) + rankorder.meanLap);
//      }
  private:

      uint8_t _lastSecondView = millis() / 1000; // seconds
      bool _isReady = true;
};

struct racerMemory racerBuffer[NUM_BUFFER];


class Race {
  public:
      bool raceState = false;
      bool connectState = false;
      uint8_t numberTotalLaps = 10; // todo: default, should be a define ?
      uint32_t startRaceMillis = 0;
      uint32_t currentRaceMillis = 0;
      uint32_t endRaceMillis = 0;
      uint32_t standbyHeartbeat = 0;
      uint32_t jsonSendMillis = 0;
      const uint32_t endRaceDelay = 20 * 1000; // 20sec, timeout to exclude late racer/player and/or make a pseudo filter of false positive.
      uint8_t lastRaceState;
      uint8_t biggerLap = 0;
      
      

      Race() // todo: add number player?
      {
      }

      void loop(){
          bool trig = false;
          StaticJsonDocument<300> doc;
          JsonObject lock = doc.createNestedObject("lock");
          
          switch (raceStateENUM) {
              case DISCONNECTED:
                  // only enable Connected button
                  // disable every others button/progress bar/cursor
                  if (connectState) {
                      connectState = false;
                      doc["message"] = "Disconnected!";
                      doc["connect"] = 0;
                      lock["start"] = 1;
                      lock["light"] = 0;
                      lock["connect"] = 0;
                      lock["lapsSlider"] = 1;
                  }
                  break;
                  
              case CONNECTED:
                  // when connected, disable connect button, enable everything
                  // go back to disconnected if no web client and no lcd/oled?
                  // waiting USER to start a new Race. goto INIT
                  if (!connectState) { // Add sanety check... ie: 20x %A&
                      connectState = true;
                      doc["connect"] = 1;
                      lock["start"] = 0;
                      lock["light"] = 0;
                      lock["connect"] = 0;
                      lock["lapsSlider"] = 0;
                      doc["message"] = "Connected!";
                  }

                //   if (connectState){
                //       if (millis() - standbyHeartbeat > 5000){
                //           raceStateENUM = DISCONNECTED;
                //       }
                //   }
                  break;

              case SET:
                  for (uint8_t i = 0; i < NUM_BUFFER; i++){
                      racerBuffer[i].reset();
                  }
                  startRaceMillis = millis();
                  raceStateENUM = RACE;
                  lock["connect"] = 1;
                  lock["lapsSlider"] = 1;
                  doc["stopwatch"] = 1;
                  doc["message"] = "RUN RUN RUN";
                  break;

              case RACE:
                  // update dataRacerLoop
                  // the first player have finished, goto END
                  // CHECK if numberTotalLaps have been passed
                  updateDataLoop();
                  if (biggerLap == numberTotalLaps - 1){
                      doc["message"] = "Last Lap";
                  }
                  if (biggerLap == numberTotalLaps){
                      endRaceMillis = millis();
                      raceStateENUM = END;
                      doc["message"] = "Hurry Up !";
                  }
                  break;

              case END:
                  // let others finish with a 20sec delay. (don't count if lap > totalLap.)
                  // goto RESET (permit a manual user STOP);
                  updateDataLoop(); //Special Mode!!
                  if (millis() - endRaceMillis > endRaceDelay){
                      raceStateENUM = RESET;
                      doc["message"] = "Finished !";
                  }
                  break;

              case RESET:
                  // Stop everything, reset all counter...
                  // goto CONNECTED
                  raceStateENUM = CONNECTED;
                  lock["connect"] = 0;
                  lock["lapsSlider"] = 0;
                  doc["stopwatch"] = 0;
                  doc["race"] = 0;
                  zeroing();
                  aRankOrder = (const struct rankOrder){0};
                  break;

              default:
                  break;
          }

          if (lastRaceState != raceStateENUM) {
              lastRaceState = raceStateENUM;
              trig = true;
          }

          if (raceStateENUM > 1) {
              if (millis() - jsonSendMillis > 1000) {
                  jsonSendMillis = millis();
                  doc["percentLap"] = (uint8_t)getBiggestLap() / getLaps() * 100;
                  doc["lapCounter"] = getBiggestLap();
                 trig = true;
              }
          }
          
          if (trig) {
              if (!doc.isNull()) {
                  char json[300];
                  serializeJsonPretty(doc, json);
                  ws.textAll(json);
              }
          }
      }

      void setLaps(uint8_t laps) {
          numberTotalLaps = laps;
      }

      uint8_t getLaps() {
          return numberTotalLaps;
      }

      void setBiggestLap(uint8_t laps) {
          biggerLap = laps;
      }

      uint8_t getBiggestLap() {
          return biggerLap;
      }
      
      void setPing() {
          standbyHeartbeat = millis();
      }
   private:
      const uint16_t cleanerDelay = 1000;
      uint32_t cleanerTimer = millis();

      void zeroing(){
          raceState = false;
          startRaceMillis = 0;
          currentRaceMillis = 0;
          endRaceMillis = 0;
          standbyHeartbeat = millis();
          jsonSendMillis = 0;
          biggerLap = 0;
      }
    //   void sendJson(){      
    //   }
};
Race raceLoop = Race();


//=============== Web Stuff/config ============================

void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

// callback for websocket event.
void onWSEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               AwsEventType type,
               void* arg,
               uint8_t* data,
               size_t length) {
  switch (type) {
    case WS_EVT_CONNECT: 
      {
        Serial.println("WS client connect");
        DynamicJsonDocument doc(JSON_BUFFER);
        doc["light"] = lightState ? 1 : 0;
        doc["connect"] = (raceStateENUM > 1) ? 1 : 0;
        doc["race"] = (raceStateENUM > 3) ? 1 : 0;
        doc["setlaps"] = raceLoop.getLaps();
        char json[JSON_BUFFER];
        serializeJsonPretty(doc, json);
        client->text(json);
      }
      break;
    case WS_EVT_DISCONNECT:
      Serial.println("WS client disconnect");
      break;
    case WS_EVT_DATA:
      {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->final && (info->index == 0) && (info->len == length)) {
          if (info->opcode == WS_TEXT) {
            bool trigger = false;
            data[length] = 0;
            // Serial.print("data is ");
            // Serial.println((char*)data);
            DynamicJsonDocument doc(JSON_BUFFER);
            deserializeJson(doc, (char*)data);
            if (doc.containsKey("race")) {
              raceStateENUM = (uint8_t)doc["race"] ? SET : RESET;
//              raceState = doc["race"];
              trigger = true;
            }
            if (doc.containsKey("light")) {
              lightState = doc["light"];
              trigger = true;
            }
            if (doc.containsKey("setlaps")) {
              raceLoop.setLaps((uint8_t)doc["setlaps"]);
              trigger = true;
            }
            if (doc.containsKey("connect")) {
              raceStateENUM = (uint8_t)doc["connect"] ? CONNECTED : DISCONNECTED;
//              connectState = doc["connect"];
              trigger = true;
            }
            if (trigger){ //why...?
                char json[JSON_BUFFER];
                serializeJsonPretty(doc, json);
                // Serial.print("Sending ");
                // Serial.println(json);
                ws.textAll(json);
            }
          } else {
            Serial.println("Received a ws message, but it isn't text");
          }
        } else {
          Serial.println("Received a ws message, but it didn't fit into one frame");
        }
      }
      break;
  }
}

void server_init()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/favicon.ico", "image/ico");
  });
  
  // Route to load css files
  server.on("/css/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/css/style.css", "text/css");
  });
  server.on("/css/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/css/bootstrap.min.css", "text/css");
  });
  server.on("/css/bootstrap.min.css.map", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/css/bootstrap.min.css.map", "text/css");
  });

  // Route to load javascript files
  server.on("/js/jquery.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/js/jquery.min.js", "text/javascript");
  });
  server.on("/js/bootstrap.bundle.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/js/bootstrap.bundle.min.js", "text/javascript");
  });
  server.on("/js/bootstrap.bundle.min.js.map", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/js/bootstrap.bundle.min.js.map", "text/javascript");
  });
  server.on("/js/scripts.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/js/scripts.js", "text/javascript");
  });

//  server.on("/pressure", HTTP_GET, [](AsyncWebServerRequest *request){
//    request->send_P(200, "text/plain", getPressure().c_str());
//  });
//  server.on("/",HTTP_GET, handleRoot);

  server.begin();

  ws.onEvent(onWSEvent);
  server.addHandler(&ws);
}

void button_init()
{
    btn1.setDebounceTime(50);
    btn2.setDebounceTime(50);
    
    btn1.setTapHandler([](Button2& btn) {
        Serial.println("A clicked");
        static bool state = false;
        state = !state;
        tft.fillRect(0, 100, 120, 35, state ? TFT_WHITE : TFT_BLACK);
    });

    btn2.setTapHandler([](Button2& btn) {
        static bool state = false;
        state = !state;
        Serial.println("B clicked");
        tft.fillRect(120, 100, 120, 35, state ? TFT_WHITE : TFT_BLACK);
    });
}


// Screen sort of led status... 
class Led // lKeep RED and GREEN led active
{
private:
    uint32_t OnTime = 1000;     // milliseconds of on-time
    uint32_t OffTime = 1000;    // milliseconds of off-time
    bool ledState = LOW;                 // ledState used to set the LED
    uint32_t previousMillis;   // will store last time LED was updated
 
    void setOutput(bool state_, uint32_t currentMillis_){
        ledState = state_;
        previousMillis = currentMillis_;
        digitalWrite(_ledPin, state_);
    }

public:
    Led(uint16_t x_start,uint16_t y_start, uint16_t x_end, uint16_t y_end)
    {
        this->_ledPin = _ledPin;
        pinMode(_ledPin, OUTPUT);
        previousMillis = 0; 
    }

    void set(uint32_t on, uint32_t off){
        OnTime = on;
        OffTime = off;
    }

    void loop(){
        uint32_t currentMillis = millis();
            
        if((ledState == HIGH) && (currentMillis - previousMillis >= OnTime))
        {
            setOutput(LOW, currentMillis);
        }
        else if ((ledState == LOW) && (currentMillis - previousMillis >= OffTime))
        {
            setOutput(HIGH, currentMillis);
        }
    }
};

Led tftLed = Led(tft.width - 10, tft.height - 10, tft.width, tft.height); // position X-start, Y-start, X-end, Y-end
class Display
{
private:



public:


};

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
  server_init();
  button_init();

//  AsyncElegantOTA.begin(&server);
//  server.on("/",HTTP_GET, handleRoot);

  IrReceiver.begin(IR_RECEIVE_PIN, false);

  for (int i = 0 ; i < CAR_NUMBER ; i++)
  {
     irCode[i].irCode = CAR_IRCODE_START + i;
  }
  
  
  tft.init();
  tft.setRotation(1);

  for (uint8_t i = 5; i != 0; i--){
      tft.setCursor(120 - 15, 70 - 25);
      tft.setTextSize(5);
      tft.setTextColor(TFT_RED);
      tft.fillScreen(TFT_BLACK);
      tft.print(i);
      delay(200);
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
  static uint32_t refreshTimer = millis();
  const uint32_t refreshDelay = 100;
  static uint32_t fastLoopTimer = millis(); // change to micros?
  const uint32_t fastLoopDelay = 1;
  static uint32_t slowLoopTimer = millis();
  const uint32_t slowLoopDelay = 1000;
  static uint32_t counter = 0;
  static bool blinkState = false;

  btn1.loop();
  btn2.loop();
  raceLoop.loop();

  static uint32_t cleanerTimer = millis();
  const uint32_t cleanerDelay = 1000;
  
  // Only connected, and waiting...
  if (raceStateENUM > 0) {
      uint16_t irCode = isAnIrCode();
      if (irCode != 0) {
          // processingData();
          addLaps(irCode); // 
      }
  }


  if (millis() - refreshTimer > refreshDelay){
    refreshTimer = millis();
    tft.fillRect(70, 10, 110, 15, TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(70,10);
    tft.print(counter);
  }
  if (millis() - fastLoopTimer > fastLoopDelay){
    fastLoopTimer = millis();
    counter++;
  }
  if (millis() - blinkTimer > slowLoopDelay){
    slowLoopDelay = millis();
    blinkState = !blinkState;
    tft.fillRect(0, 0, 240, 1, blinkState ? TFT_RED : TFT_BLACK);
    //        Serial.printf("[RAM: %d]\r\n", esp_get_free_heap_size());
  }
//  irLoop();
}


void addLaps(uint8_t idHex){
// That function update the Struct, or init it.
  // static bool fullBuffer = false;
  bool updated = false;
  uint8_t color = 0;
  uint32_t timeDec = millis() - startRaceMillis;
  // test if idDec is already memorized,
  // if not check the first available racerBuffer. ie: check if .id == 0;
  // if all full (4slots for now), Serial.print(error);
  // First pass try to update Racer
  for (uint8_t i = 0; i < NUM_BUFFER; i++) {
      if (racerBuffer[i].id == idHex)
      {
          if (racerBuffer[i].getState(DEBOUNCE_SECOND))
          {
              if (racerBuffer[i].numberLap < raceLoop.getLaps()) {
                  racerBuffer[i].setCurrentLap(timeDec);
                  updated = true;
                  debugPrintRacer(i);
              }
          }

          break; // speedup
      }
  }
  
  // Set and init racer if buffer available
  if (!updated && timeDec < TOO_LATE_FOR_INIT_RACER){ // 20sec after that not possible to get new driver!
      for (uint8_t i = 0; i < CAR_NUMBER; i++){
          if (racerBuffer[i].id == 0){
              racerBuffer[i].rank = i;
              racerBuffer[i].color = i + 1;
              racerBuffer[i].init(idHex, timeDec);
//               debugPrintRacer(i);
              break; //finish registration
          }
          else
          {
            //  fullBuffer = true;
             Serial.println("Maximum memorized ID");
          }
      }
  }
}


//check rank change with numberLaps.
bool isRankChanged(){
  static totalLapsLast = 0;
  uint16_t totalLaps = 0;
  bool isChanged = false;

  for (uint8_t i = 0; i < CAR_NUMBER; i++){
      totalLaps +=  aRankOrder.laps[i];
  }

  if (totalLaps != totalLapsLast)
  {
      totalLapsLast = totalLaps;
      isChanged = true;
  }

  return isChanged;
}


void updateRank(){
  //who have changed algo.
  uint8_t idChanged = CAR_NUMBER + 1; // define an illegal ID number as 0/null == first array too...
  uint8_t idRank;
  // static uint8_t te;  

  for (uint8_t i = 0; i < CAR_NUMBER; i++){ // find idChanged
      if (aRankOrder.laps[i] != aRankOrder.prevLaps[i])
      {
          aRankOrder.prevLaps[i] = aRankOrder.laps[i];
          idChanged = i;
          for (uint8_t j = 0; i < CAR_NUMBER; j++){ // find  idRank
              if (aRankOrder.rank[j] == idChanged)
              {
                  idRank = j;
                  break;
                  // aRankOrder.numberRegistered++;
              }
          }
          break; //speedup
      }
  }
  
  if (idChanged <  CAR_NUMBER + 1) // Check if it's not a illegal ID.
  {
      for (uint8_t i = 0; i < idRank; i++){
          uint8_t idTmp = idRank - i;
          uint8_t idAhead = aRankOrder.rank[idTmp - 1];

          if (aRankOrder.laps[idChanged] > aRankOrder.laps[idAhead]){
              aRankOrder.rank[idTmp - 1] = idChanged;
              aRankOrder.rank[idTmp] = idAhead;
          }
          else
          {
            break;
          }
      }
  }
}


// completely change this algo!!!
void updateDataLoop(){ //run at each interval OR/AND at each event (updateRacer), that is the question...
    bool trigger = false;
    static uint8_t posUpdate = 0;
    static bool needJsonUpdate = false;
    static bool needLapUpdate = false;
    static uint32_t websockCount = 0;
    const uint16_t websockDelay = 200;
    static uint32_t websockTimer = millis();
    static uint32_t websockTimerFast = millis();
    static uint16_t lastTotalLap = 0;
    static uint8_t biggestTotalLap = 0;
//    struct racerMemory tmpRacerBuffer;
    uint32_t tmpRacerBuffer[9] = {0}; // id, numberLap, offsetLap, lastLap, bestLap, meanLap, totalLap
    const String orderString[] = {"one", "two", "three", "four"}; //change by char...
    uint16_t tmpTotalLap = 0;

    for (uint8_t i = 0; i < NUM_BUFFER; i++){
        tmpTotalLap += racerBuffer[i].numberLap;
        if (racerBuffer[i].numberLap > raceLoop.getBiggestLap()){
            raceLoop.setBiggestLap(racerBuffer[i].numberLap);
        }

       if ( i < NUM_BUFFER - 1){
            if (racerBuffer[i + 1].numberLap > racerBuffer[i].numberLap)
            { // My worst code ever, but working code... a way to copy a complete struct without got an esp exception...
                tmpRacerBuffer[0] = racerBuffer[i].id;
                tmpRacerBuffer[1] = racerBuffer[i].numberLap; 
                tmpRacerBuffer[2] = racerBuffer[i].offsetLap;
                tmpRacerBuffer[3] = racerBuffer[i].lastLap;
                tmpRacerBuffer[4] = racerBuffer[i].bestLap;
                tmpRacerBuffer[5] = racerBuffer[i].meanLap;
                tmpRacerBuffer[6] = racerBuffer[i].totalLap;
                tmpRacerBuffer[7] = racerBuffer[i]._tmpLast;
                tmpRacerBuffer[8] = racerBuffer[i].color;
                
                racerBuffer[i].id = racerBuffer[i + 1].id;
                racerBuffer[i].numberLap = racerBuffer[i + 1].numberLap;
                racerBuffer[i].offsetLap = racerBuffer[i + 1].offsetLap;
                racerBuffer[i].lastLap = racerBuffer[i + 1].lastLap;
                racerBuffer[i].bestLap = racerBuffer[i + 1].bestLap;
                racerBuffer[i].meanLap= racerBuffer[i + 1].meanLap;
                racerBuffer[i].totalLap = racerBuffer[i + 1].totalLap;
                racerBuffer[i]._tmpLast = racerBuffer[i + 1]._tmpLast;
                racerBuffer[i].color = racerBuffer[i + 1].color;
                
                racerBuffer[i + 1].id = tmpRacerBuffer[0];
                racerBuffer[i + 1].numberLap = tmpRacerBuffer[1];
                racerBuffer[i + 1].offsetLap = tmpRacerBuffer[2];
                racerBuffer[i + 1].lastLap = tmpRacerBuffer[3];
                racerBuffer[i + 1].bestLap = tmpRacerBuffer[4];
                racerBuffer[i + 1].meanLap = tmpRacerBuffer[5];
                racerBuffer[i + 1].totalLap = tmpRacerBuffer[6];
                racerBuffer[i + 1]._tmpLast = tmpRacerBuffer[7];
                racerBuffer[i + 1].color = tmpRacerBuffer[8];
                break;
            }
       }
    }
    
    if (tmpTotalLap != lastTotalLap || tmpTotalLap == 0){
        lastTotalLap = tmpTotalLap;
        needJsonUpdate = true;
    }
    
    DynamicJsonDocument doc(JSON_BUFFER);

    if( millis() - websockTimerFast > websockDelay){
        currentRaceMillis = millis() - startRaceMillis;
        websockTimerFast = millis();
        doc["websockTimer"] = websockTimer;

        JsonObject data = doc.createNestedObject("data");

        if (millis() - websockTimer > websockDelay / 2 && needJsonUpdate){ //change condition by time to totalLap changed
            websockTimer = millis();
            data[orderString[posUpdate] + "_class"] = racerBuffer[posUpdate].color;
            data[orderString[posUpdate] + "_id"] = racerBuffer[posUpdate].id;
            data[orderString[posUpdate] + "_lap"] = racerBuffer[posUpdate].numberLap;
            data[orderString[posUpdate] + "_last"] = millisToMSMs(racerBuffer[posUpdate].lastLap);
            data[orderString[posUpdate] + "_best"] = millisToMSMs(racerBuffer[posUpdate].bestLap);
            data[orderString[posUpdate] + "_mean"] = millisToMSMs(racerBuffer[posUpdate].meanLap);
            data[orderString[posUpdate] + "_total"] = millisToMSMs(racerBuffer[posUpdate].totalLap);
            data[orderString[posUpdate] + "_current"] = racerBuffer[posUpdate].totalLap; //yes i know...
            posUpdate++; 
            if (posUpdate == NUM_BUFFER){
              posUpdate = 0;
              needJsonUpdate = false;
            }
        }
 
        trigger = true;
    }

    char json[JSON_BUFFER];
    serializeJsonPretty(doc, json);

    if (trigger){
        ws.textAll(json);
    }
}


void processingData(){ // char* data, uint8_t dataSize){
}


uint16_t isAnIrCode() {
    uint16_t irCode = 0;
    bool irCodeValid = false;

    if (IrReceiver.decode()) {
//        IrReceiver.printIRResultMinimal(&Serial);
        if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_WAS_OVERFLOW) {
            IrReceiver.decodedIRData.flags = false; // yes we have recognized the flag :-)
            Serial.println(F("Overflow detected"));
        } else {
//            IrReceiver.printIRResultShort(&Serial);
            if (IrReceiver.decodedIRData.protocol == UNKNOWN || digitalRead(DEBUG_BUTTON_PIN) == LOW) {
//              IrReceiver.printIRResultRawFormatted(&Serial, true);
                Serial.println(F("UNKNOWN"));
            }
            else if (IrReceiver.decodedIRData.decodedRawData == NEC) {
                irCodeValid = true;
            }
        }

        IrReceiver.resume();

        if (IrReceiver.decodedIRData.command == 0x42 && irCodeValid) {
            irCode = IrReceiver.decodedIRData.adress;
        }
        //else add robitronic/lapz proto here as they don't have .command/.address
    }
    return irCode;
}


//  uint16_t tmpIrCode = isAnIrCodeLoop();
//  if (tmpIrCode > 0)
//  {
//      for (int i = 0 ; i < CAR_NUMBER ; i++)
//      {
//          if (irCode[i].irCode == tmpIrCode && irCode[i].getState(DEBOUNCE_SECOND))
//          {
//              selectedSerial->print("%L");
//              selectedSerial->print(irCode[i].irCode, HEX);
//              selectedSerial->print(",");
//              selectedSerial->print(currentTime, HEX);
//              selectedSerial->print("&");
//              selectedSerial->println();
//          }
//      }
//  }






// Better to process on the mcu c++ or the browser javascript side... ??
// But as there is already an identical function on the javascript side used for current time, remove that one ?
String millisToMSMs(uint32_t tmpMillis){
    uint32_t millisec;
    uint32_t total_seconds;
    uint32_t total_minutes;
    uint32_t seconds;
    String DisplayTime;

    total_seconds = tmpMillis / 1000;
    millisec  = tmpMillis % 1000;
    seconds = total_seconds % 60;
    total_minutes = total_seconds / 60;

    String tmpStringMillis;
    if (millisec < 100)
      tmpStringMillis += '0';
    if (millisec < 10)
      tmpStringMillis += '0';
    tmpStringMillis += millisec;

    String tmpSpringSeconds;
    if (seconds < 10)
      tmpSpringSeconds += '0';
    tmpSpringSeconds += seconds;

    String tmpSpringMinutes;
    if (total_minutes < 10)
      tmpSpringMinutes += '0';
    tmpSpringMinutes += total_minutes;
    
    DisplayTime = tmpSpringMinutes + ":" + tmpSpringSeconds + "." + tmpStringMillis;

    return DisplayTime;
}