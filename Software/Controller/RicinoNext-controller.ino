#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsync_WiFiManager.h>
#include <ESPAsyncWebServer.h>
//#include <AsyncElegantOTA.h>;

#include <FS.h>
#include "SPIFFS.h"

#include <ArduinoJson.h>

#include <Wire.h>          

// DEBUG value for simulate fake receiver/emitter!!!!
uint32_t startMillis;
const uint32_t idList[] = { 0x1234, 0x666, 0x1337, 0x2468}; // 0x4321, 0x2222, 0x1111, 0x1357};
uint16_t idListTimer[] = { 2000, 2050, 2250, 2125}; // , 2050, 2150, 2250, 2350}; // used for the first lap!
uint32_t idListLastMillis[] = { 0, 0, 0, 0}; // , 0, 0, 0, 0,};
uint8_t idGateNumber[] = { 20, 20, 20, 20}; // , 19, 19, 19, 19}; // Address of first gate - 1
// END DEBUG value

/* AsyncWebServer Stuff */
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

#define JSON_BUFFER 512

//#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include "Button2.h"

//TFT_eSPI tft = TFT_eSPI(135, 240);  // Invoke library, pins defined in User_Setup.h
//#define TFT_GREY 0x5AEB // New colour

#define NUMBER_RACER 4 // todo/future: set a fixed 32 struct BUT dynamic player number? to avoiding malloc/free etc..
#define ARRAY_LENGTH(x)  ( sizeof( x ) / sizeof( *x ) )

const uint8_t addressAllGates[] = {21, 22, 23}; // Order: 1 Gate-> ... -> Final Gate! | define number in UI ?
#define NUMBER_GATES ( sizeof( addressAllGates ) / sizeof( *addressAllGates ) )

#define NUMBER_PROTOCOL 3 // 0:serial, 1:ESP/JSON, 2:tft


// Physical Button on TTGO Display
#define BUTTON_1            35
#define BUTTON_2            0

Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

uint16_t ledPin = 13;

// prototype
void sortIDLoop(); // maybe add to Race class in future
void fakeIDtrigger(int ms); //debug function (replace i2c connection)


/*================ UI Config struct =========================*/
// That struct is directly changed from client UI.
// Auto send to client when isChanged or at new connection

struct UI_config{
    bool isChanged = false;

    uint16_t set_laps = 10; // 1 - ? unlimited ?
    uint8_t set_number_players = NUMBER_RACER; // 1 - ? 32 max ? (ESP memory/speed limit)
    uint8_t set_number_gates = NUMBER_GATES; // 1…?8 max?

    bool set_light = 0;
    uint8_t set_brightness_light = 255;

    // Todo Add sound/voice/etc... here

    enum state_t {
        RESET,     // 
        START,     // forbid any UI modifiction except STOP!
        STOP       // only allow UI modification
    }setState = RESET;

    bool read_ID = 0;

    struct Player
    {
        // uint8_t position;
        uint32_t ID;
        char name[16];
        char color[8];
    }player[NUMBER_RACER];
};

UI_config uiConfig;


/* =============== Main Data struct =============== */

// One Struct to keep every player data + sorted at each loop.
typedef struct {
  public:
    // get info from i2c/gate
    uint32_t ID;
    uint32_t lapTime;
    uint8_t currentGate;

    // Calculated value:
    uint16_t laps;
//    uint32_t offsetLap;
    // Make all variable below private ?
    uint32_t bestLapTime, meanLapTime, lastLapTime, lastTotalTime;
  
    uint32_t lastTotalCheckPoint[NUMBER_GATES];
    uint32_t lastCheckPoint[NUMBER_GATES];
    uint32_t bestCheckPoint[NUMBER_GATES];
    uint32_t meanCheckPoint[NUMBER_GATES];
    uint32_t sumCheckPoint[NUMBER_GATES]; // 1193Hours for a buffer overflow?

    bool haveUpdate[NUMBER_PROTOCOL][NUMBER_GATES];
    bool positionChange[NUMBER_PROTOCOL]; // serial, tft, web,... add more


    // todo
    int8_t statPos;
    uint8_t lastPos;
    bool haveMissed;
    uint8_t lastGate;
    bool haveInitStart = 0;
    uint8_t indexToRefresh; 


    // bool needTFTUpdate[NUMBER_GATES];
    // bool needSerialUpdate[NUMBER_GATES]; // to update only changed gate

  void reset(){
    ID, laps = 0;
    lapTime, lastTotalTime, bestLapTime, meanLapTime, lastLapTime = 0;
    currentGate = addressAllGates[0];

    memset(positionChange, 0, sizeof(positionChange)); // serial, tft, web,... add more
    memset(haveUpdate, 0, sizeof(haveUpdate)); // false?

    memset(lastTotalCheckPoint, 0, sizeof(lastTotalCheckPoint));
    memset(lastCheckPoint, 0, sizeof(lastCheckPoint));
    memset(bestCheckPoint, 0, sizeof(bestCheckPoint));
    memset(meanCheckPoint, 0, sizeof(meanCheckPoint));
    memset(sumCheckPoint, 0, sizeof(sumCheckPoint));
  }

  void updateTime(uint32_t timeMs, uint8_t gate){
    currentGate = gate;
    lapTime = timeMs;
//    uint32_t tmp_lastTotalLapTime = totalLapTime;
    uint8_t idxGates = gateIndex(gate);

    if (haveInitStart){
      // one complete lap done
      if (currentGate == addressAllGates[0]){
        // add only if " < race.numberTotalLaps ?
        laps++;
        // Calculation of full lap!
        lastCalc(&lastLapTime, &lastTotalTime, NULL, timeMs);
        meanCalc(&meanLapTime, NULL, timeMs);
        bestCalc(&bestLapTime, lastLapTime);
      }

      // Calculation of check point passage
      uint8_t prevIndex = (idxGates == 0) ? ((uint8_t)NUMBER_GATES - 1) : (idxGates - 1);
  
      for (uint8_t i = 0; i < NUMBER_PROTOCOL; i++)
      {
        haveUpdate[i][idxGates] = true;
      }
      lastCalc(&lastCheckPoint[idxGates], &lastTotalCheckPoint[idxGates], &lastTotalCheckPoint[prevIndex], timeMs);
      meanCalc(&meanCheckPoint[idxGates], &sumCheckPoint[idxGates], lastCheckPoint[idxGates]);
      bestCalc(&bestCheckPoint[idxGates], lastCheckPoint[idxGates]);      
    }
    else
    {
      haveInitStart = true;
      // initOffset()?
    }
  }

  // todo, know if upm down, same.
  void updateRank(uint8_t currentPosition){
    statPos = lastPos - currentPosition;
    lastPos = currentPosition;
  }

  // serial, tft, web,... add more
  bool needGateUpdate(uint8_t protocol){
    bool boolUpdate = false;
    for (uint8_t i = 0; i < NUMBER_GATES; i++)
    {
      if (haveUpdate[protocol][i] == true)
      {
        haveUpdate[protocol][i] = false;
        indexToRefresh = i;
        boolUpdate = true;
        break; // don't need to check for any other change, will be done next time
      }
    }
    return boolUpdate;
  }

  private:


    uint8_t gateIndex(uint8_t gate){
      uint8_t idx;
      for (uint8_t i = 0; i < NUMBER_GATES; i++)
      {
        if (gate == addressAllGates[i])
        {
            idx = i;
            break; // idx found!
        }
      }
      return idx;
    }

    void bestCalc(uint32_t* best_ptr, uint32_t lastTime){
        if (lastTime < *best_ptr || *best_ptr == 0)
        {
            *best_ptr = lastTime;
        }
    }

    void meanCalc(uint32_t* mean_ptr, uint32_t* sum_ptr, uint32_t fullTime){
        uint32_t sumValue;
        if (sum_ptr == nullptr)
        { // full lap
            sumValue = fullTime;
        }
        else
        { // checkpoint
            *sum_ptr = *sum_ptr + fullTime;
            sumValue = *sum_ptr;
        }
        // maybe remove division and change with bitwise and multiplication.
        *mean_ptr = sumValue / ((laps > 0) ? laps : 1);
    }

    void lastCalc(uint32_t* last_ptr, uint32_t* lastTotal_ptr, uint32_t* lastPrevTotal_ptr, uint32_t fullTime){
        uint32_t value;
        if (lastPrevTotal_ptr == nullptr)
        { // full laps
          *last_ptr = fullTime - *lastTotal_ptr;
        }
        else
        { // checkpoint
          *last_ptr  = fullTime - *lastPrevTotal_ptr;
        }
        *lastTotal_ptr = fullTime;

        // Example:
        //  0s   4s   7s       10s  13s  16s
        //  |    |    |    ->   |    |    |
    }

} ID_Data_sorted;

ID_Data_sorted idData[NUMBER_RACER + 1]; // number + 1, [0] is the tmp for rank change, and so 1st is [1] and not [0] and so on...


/* ================ Buffer Struct ====================*/
// Sort-Of simple buffer
// Need to add color/name here ?
typedef struct {
    uint32_t ID = 0;
    uint8_t gateNumber = 0;
    uint32_t totalLapsTime = 0; // in millis ?
    bool isNew = false; //
} ID_buffer;

ID_buffer idBuffer[NUMBER_RACER];

//=============== Race Class =================================

enum race_state_t {
    RESET,         // Set parameters(Counter BIP etc...), before start counter, re[set] all struct/class
    WAIT,          // only check gate status, waiting for START call.
    START,         // Run the warm-up phase, (light, beep,etc) (no registering player for the moment)
    RACE,          // enable all gate receiver and so, update Race state (send JSON, sendSerial) etc...
    FINISH,        // 1st player have win, terminate after 20s OR all players have finished.
    STOP           // finished auto/manual 
};

race_state_t raceState = RESET;
// Human readable ENUM.
const char* raceStateChar[] = {"RESET", "WAIT", "START", "RACE", "FINISH", "STOP"};


class Race {
  private:
    // race_state_t oldRaceState = raceState;
    uint16_t delayWarmupDelay = 2 * 1000; // need to be dynamic/changeable from UI
    uint16_t delayWarmupTimer;
    bool isReady = false;
    uint16_t biggestLap;
    uint16_t numberTotalLaps = 5; // add pointer to uiConfig
    uint32_t finishRaceMillis;
    uint32_t finishRaceDelay = 2 * 1000; // 10sec, need changeable from UI
    race_state_t oldRaceState;
    const uint8_t messageLength = 128;
    char message[128] = "test Char pointer";
    uint32_t startTimeOffset;


//      APP_Data *app_ptr;
//      app_ptr = &appData;

    void init(){
        biggestLap = 0;
        isReady = false;
        finishRaceMillis = 0;
        delayWarmupTimer = 0;
        oldRaceState = raceState;
        
        for (uint8_t i = 0 ; i < (NUMBER_RACER + 1); i++ )
        {
            idData[i].reset();
        }
    }

    void printSerialDebug(){
        if (oldRaceState != raceState)
        {
            Serial.println(raceStateChar[oldRaceState]);
            oldRaceState = raceState;
        }
    }

  public:
    Race()
    {
    }
    // ~Race();
    
    void loop(){

        printSerialDebug();
//  
//      gatePing();
//  
        switch (raceState)
        {
        // Reset everthing (struct,class,UI,JSON)
        case RESET:
            init();
            raceState = WAIT;
            break;

        // only check Gate state ? maybe make an alive ping to JSON client.. ?
        case WAIT:
            break;

        case START:
            if (!isReady)
            {
                init();
                delayWarmupTimer = millis();
                isReady = true;
            }
    
            if (millis() - delayWarmupTimer > delayWarmupDelay){
                // for (uint8_t i = 0; i < NUMBER_GATES; i++)
                // {
                //     setCommand(addressAllGates[i], app_ptr->startCmd, sizeof(&app_ptr->startCmd));
                // }
                // app_ptr->startOffset = millis();  //(*ptr).offsetLap;

                startTimeOffset = millis();
                raceState = RACE;
            }
            break;

        case RACE:
            fakeIDtrigger(millis());

            sortIDLoop(); // processing ID.

            // for (uint8_t i = 0 ; i < NUMBER_GATES; i++)
            // {
            //     getDataFromGate(addressAllGates[i]);
            // }
            // test if first player > max LAP

            if (biggestLap == numberTotalLaps - 1)
            {
            }
            if (biggestLap == numberTotalLaps)
            {
                finishRaceMillis = millis();
                raceState = FINISH;
            }

            break;

        case FINISH:
            fakeIDtrigger(millis());

            sortIDLoop();
            // if ALL player > MAX LAP or timeOUT
            if (millis() - finishRaceMillis > finishRaceDelay)
            {
                raceState = STOP;
            }
            break;

        case STOP:
            isReady = false;
            raceState = WAIT;
            break;
        
        default:
            break;
        }
    }

    void setTotalLaps(uint16_t laps) {
        numberTotalLaps = laps;
    }

    void setBiggestLap(uint16_t lap){
        if (lap > biggestLap)
        {
            biggestLap = lap;
        }
    }

    uint16_t getBiggestLaps(){
      return biggestLap;
    }

    uint32_t getCurrentTime(){
      if (raceState == RACE || raceState == FINISH)
      {
        return ( millis() - startTimeOffset );
      }
      else
      {
        return 0;
      }
    }

    bool isIdRunning(uint8_t lap){
        return ( lap ==  numberTotalLaps )  ? false : true;
    }

    void setMessage(char *test_char[128]){
      memcpy(message, test_char, sizeof(test_char[0])*128);
    }

    char* getMessage(){
      char message_tmp[128];

//      if (message[0] != 0)
//      {
//          memcpy(message_tmp, message, sizeof(message[0])*128);
//          message[0] = 0;
//          return NULL;
//      }
      return message;
    }
};

Race race = Race();


//=============== Web Stuff/config ============================

void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

// callback for websocket event.
void readJsonInterrupt(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               AwsEventType type,
               void* arg,
               uint8_t* data,
               size_t length) {
  switch (type) {
    // Send all current player data?, current config data, and current state !
    case WS_EVT_CONNECT: 
      {
        Serial.println("WS client connect");
        // Send all current Config!
//         DynamicJsonDocument doc(JSON_BUFFER);
// //        doc["light"] = lightState ? 1 : 0;
// //        doc["connect"] = (raceStateENUM > 1) ? 1 : 0;
// //        doc["race"] = (raceStateENUM > 3) ? 1 : 0;
// //        doc["setlaps"] = raceLoop.getLaps();
//         char json[JSON_BUFFER];
//         serializeJsonPretty(doc, json);
        // client->text(json);
      }
      break;

    // Broadcast message to every clients, maybe stop, if no more client?
    case WS_EVT_DISCONNECT:
      Serial.println("WS client disconnect");
      break;

    // update config, set name/color etc...
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
//              raceStateENUM = (uint8_t)doc["race"] ? SET : RESET;
//              raceState = doc["race"];
              trigger = true;
            }
            if (doc.containsKey("light")) {
//              lightState = doc["light"];
              trigger = true;
            }
            if (doc.containsKey("setlaps")) {
//              raceLoop.setLaps((uint8_t)doc["setlaps"]);
              trigger = true;
            }
            if (doc.containsKey("connect")) {
//              raceState = (uint8_t)doc["connect"] ? CONNECTED : DISCONNECTED;
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

  server.begin();

  ws.onEvent(readJsonInterrupt);
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
//        tft.fillRect(0, 100, 120, 35, state ? TFT_WHITE : TFT_BLACK);
    });

    btn2.setTapHandler([](Button2& btn) {
        static bool state = false;
        state = !state;
        Serial.println("B clicked");
//        tft.fillRect(120, 100, 120, 35, state ? TFT_WHITE : TFT_BLACK);
    });
}


// Screen sort of led status... 
class Led // Keep RED and GREEN led active
{
private:
    uint16_t _ledPin;
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
    Led(uint16_t _lepPin)
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
            
        if((ledState == HIGH) && (currentMillis - previousMillis > OnTime))
        {
            setOutput(LOW, currentMillis);
        }
        else if ((ledState == LOW) && (currentMillis - previousMillis > OffTime))
        {
            setOutput(HIGH, currentMillis);
        }
    }
};

Led ledState = Led(ledPin);

//Led tftLed = Led(tft.width - 10, tft.height - 10, tft.width, tft.height); // position X-start, Y-start, X-end, Y-end

void setup(void) {
  Serial.begin(9600);

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  ESPAsync_WiFiManager ESPAsync_wifiManager(&server, NULL , "RicinoNextAP"); // &dns
//  ESPAsync_wifiManager.resetSettings();   //reset saved settings
  ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  ESPAsync_wifiManager.autoConnect("RicinoNextAP");

//  if (WiFi.status() == WL_CONNECTED) { Serial.print(F("Connected. Local IP: ")); Serial.println(WiFi.localIP()); }
//  else { Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status())); }
  // Route to index.html + favicon.ico
  server_init();
  button_init();

//  AsyncElegantOTA.begin(&server);
//  server.on("/",HTTP_GET, handleRoot);

//  tft.init();
//  tft.setRotation(1);

  for (uint8_t i = 5; i != 0; i--){
//      tft.setCursor(120 - 15, 70 - 25);
//      tft.setTextSize(5);
//      tft.setTextColor(TFT_RED);
//      tft.fillScreen(TFT_BLACK);
//      tft.print(i);
      delay(200);
  }

//  tft.fillScreen(TFT_BLACK);
//  tft.setTextSize(1);
//  tft.setCursor(0,10);
//  tft.setTextColor(TFT_RED);
//  tft.print("Counter: ");
//  delay(100);
//  tft.setCursor(200,10);
//  tft.setTextColor(TFT_BLUE);
//  tft.print("CYCLES");
}

void loop() {

  btn1.loop();
  btn2.loop();

  race.loop();

  // add in a class ?
  WriteJSONLive(millis(), 1);
  WriteJSONRace(millis());

  // same here. make a class ?
  WriteSerialLive(millis(), 0);
  ReadSerial();
}

// Function below need to be/get called at each triggered gate.
// take ID, buffering in a temp struct before to be processed by the sortIDLoop().
void bufferingID(int ID, uint8_t gate, int totalTime){

  for (uint8_t i = 0; i < NUMBER_RACER; i++)
  {
    if (idBuffer[i].ID == ID)
    {
      idBuffer[i].totalLapsTime = totalTime;
      idBuffer[i].gateNumber = gate;
      idBuffer[i].isNew = true;
      break; // Only one ID by message, end the loop faster
    }
    else if (idBuffer[i].ID == 0)
    {
      idBuffer[i].ID = ID;
      idBuffer[i].totalLapsTime = totalTime;
      idBuffer[i].gateNumber = gate;
      idBuffer[i].isNew = true;
      for (uint8_t j = 1; j < NUMBER_RACER + 1; j++)
      {
          if ( idData[j].ID == 0)
          {
              idData[j].ID = idBuffer[i].ID;
              break; // Only registered the first Null .ID found
          }
      }
      break; // Only one ID by message, end the loop faster
    }
  }
}


// The most important loop,need to be the fastest possible (idBuffer overflow? bad sorting? who know...)
// This function checking new data in bufferingID()/idBuffer[i]
// sorting struct/table and process idBuffer -> idData.
// don't know if it's add value to integrating directly in the race private class...

void sortIDLoop(){

  // check if new data available
  for (uint8_t i = 0; i < NUMBER_RACER ; i++)
  {
    if (idBuffer[i].isNew == true)
    {
       idBuffer[i].isNew = false;
       // look ID current position
       uint8_t currentPosition = 0;
       uint32_t currentTime = idBuffer[i].totalLapsTime;
       uint8_t currentGate = idBuffer[i].gateNumber;
       

       // get current position
       for (uint8_t j = 1; j < NUMBER_RACER + 1 ; j++)
       {
         if ( idBuffer[i].ID == idData[j].ID )
         {
            currentPosition = j;
            if (race.isIdRunning(idData[currentPosition].laps))
            {
                idData[currentPosition].updateTime(currentTime, currentGate);
            }
            // update biggestLap.
            race.setBiggestLap(idData[currentPosition].laps);

            break; // Processing only one idBuffer at a time!
         }
       }
      //  check if ID get up in position/rank.
       for (uint8_t k = currentPosition; k > 1 ; k--)
       {
          if (idData[k].laps > idData[k - 1].laps)
          {
              for (uint8_t l = 0; l < NUMBER_PROTOCOL; l++)
              {
                idData[k].positionChange[l] = true; // enable change for all protocols.
                idData[k - 1].positionChange[l] = true;
              }

              idData[0] = idData[k - 1]; // backup copy
              idData[k - 1] = idData[k];
              idData[k] = idData[0];
          }
          else
          {
            break; // Useless to continu for better position
          }
       }
    }
  }
}


// Send JSON if idData change.
void WriteJSONLive(uint32_t ms, uint8_t protocol){

  // Send "live" JSON section
  // todo: maybe add a millis delay to don't DDOS client :-D
  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++){
    if (idData[i].positionChange[protocol] == true || idData[i].needGateUpdate(protocol))
    {
      idData[i].positionChange[protocol] = false;
      StaticJsonDocument<JSON_BUFFER> doc;

      // Need to fill now...
      JsonObject live = doc.createNestedObject("live");

      live["rank"] = i;
      live["id"] = idData[i].ID;
      live["lap"] = idData[i].laps;
      live["best"] = idData[i].bestLapTime;
      live["last"] = idData[i].lastLapTime;
      live["mean"] = idData[i].meanLapTime;
      live["total"] = idData[i].lastTotalTime;

      JsonArray live_gate = live.createNestedArray("gate");
      for ( uint8_t i = 0; i < NUMBER_GATES; i++)
      {
        uint8_t shiftGate = ((i + 1) < NUMBER_GATES) ? (i + 1) : 0;
        live_gate.add(idData[i].lastCheckPoint[shiftGate]);
      }

      char json[JSON_BUFFER];
      serializeJsonPretty(doc, json); // todo: remove the pretty after
      ws.textAll(json);
      break;
    }
  }
}


// Send Race state.
void WriteJSONRace(uint32_t ms){

  // Send "Race" JSON section

  static race_state_t oldRaceState = raceState;
  static uint32_t lastMillis = 0;
  uint32_t delayMillis;
  
  delayMillis = (race.getCurrentTime() == 0) ? 5000 : 1000;

  if (ms - lastMillis > delayMillis || oldRaceState != raceState)
  {
    lastMillis = millis();
    oldRaceState = raceState;

    // Need to feel now...

    StaticJsonDocument<JSON_BUFFER> doc;
    JsonObject race_json = doc.createNestedObject("race");

    race_json["state"] = raceStateChar[raceState];
    race_json["lap"] = race.getBiggestLaps();
    race_json["time"] = race.getCurrentTime();
 
    char *valueMessage = race.getMessage();
    if (valueMessage != NULL)
    {
       race_json["message"] = valueMessage;
    }

    char json[JSON_BUFFER];
    serializeJsonPretty(doc, json); // todo: remove the pretty after
    ws.textAll(json);
  }


// ----------------------------------------------------------------------------
// WebSocket initialization
// ----------------------------------------------------------------------------

// void notifyClients() {
//     const uint8_t size = JSON_OBJECT_SIZE(1);
//     StaticJsonDocument<size> json;
//     json["status"] = led.on ? "on" : "off";

//     char buffer[17];
//     size_t len = serializeJson(json, buffer);
//     ws.textAll(buffer, len);
// }

// void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
//     AwsFrameInfo *info = (AwsFrameInfo*)arg;
//     if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {

//         const uint8_t size = JSON_OBJECT_SIZE(1);
//         StaticJsonDocument<size> json;
//         DeserializationError err = deserializeJson(json, data);
//         if (err) {
//             Serial.print(F("deserializeJson() failed with code "));
//             Serial.println(err.c_str());
//             return;
//         }

//         const char *action = json["action"];
//         if (strcmp(action, "toggle") == 0) {
//             led.on = !led.on;
//             notifyClients();
//         }

//     }
// }

// void onEvent(AsyncWebSocket       *server,
//              AsyncWebSocketClient *client,
//              AwsEventType          type,
//              void                 *arg,
//              uint8_t              *data,
//              size_t                len) {

//     switch (type) {
//         case WS_EVT_CONNECT:
//             Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
//             break;
//         case WS_EVT_DISCONNECT:
//             Serial.printf("WebSocket client #%u disconnected\n", client->id());
//             break;
//         case WS_EVT_DATA:
//             handleWebSocketMessage(arg, data, len);
//             break;
//         case WS_EVT_PONG:
//         case WS_EVT_ERROR:
//             break;
//     }
// }

// void initWebSocket() {
//     ws.onEvent(onEvent);
//     server.addHandler(&ws);
// }


// JsonObject config = doc.createNestedObject("config");
// config["lap_total"] = 10;
// config["players_number"] = 4;
// config["gate_number"] = 3;
// config["light_state"] = 0;
// config["light_brightness"] = 0;

// JsonArray config_players_conf = config.createNestedArray("players_conf");

// JsonObject config_players_conf_0 = config_players_conf.createNestedObject();
// config_players_conf_0["id"] = "xxxxxxx";
// config_players_conf_0["name"] = "player01";
// config_players_conf_0["color"] = "blue";

// JsonObject config_players_conf_1 = config_players_conf.createNestedObject();
// config_players_conf_1["id"] = "xxxxxxx";
// config_players_conf_1["name"] = "player02";
// config_players_conf_1["color"] = "red";

// JsonObject config_players_conf_2 = config_players_conf.createNestedObject();
// config_players_conf_2["id"] = "xxxxxxx";
// config_players_conf_2["name"] = "player03";
// config_players_conf_2["color"] = "green";

// JsonObject config_players_conf_3 = config_players_conf.createNestedObject();
// config_players_conf_3["id"] = "xxxxxxx";
// config_players_conf_3["name"] = "player04";
// config_players_conf_3["color"] = "yellow";
// config["detected_ID"] = "XXXX";

}

// Debug Loop, simulate the gates
void fakeIDtrigger(int ms){
//   const numberGateDebug = NUMBER_GATES;

    static race_state_t oldRaceStateDebug = START;
    static bool isNew = false;
    static uint32_t timeoutReady = millis();
    const uint32_t timeoutReadyDelay = 1000; // 1sec without call this function.
    uint8_t gateNumber_tmp;
    const uint8_t lastGatesDebug = addressAllGates[NUMBER_GATES - 1];

    //detect if it's a new race
    if ( millis() - timeoutReady > timeoutReadyDelay)
    {
        isNew = false;
        Serial.println("AUTO RESET");
    }
    timeoutReady = millis();

    if ( oldRaceStateDebug == START && raceState == RACE && isNew == false)
    {
        isNew = true;
        for (uint8_t i = 0; i < NUMBER_RACER; i++)
        {
            idGateNumber[i] = 20;
        }
        oldRaceStateDebug = RACE;
        startMillis = millis();
        Serial.println("NEW");
    }

    if (isNew)
    {
        for (uint8_t i = 0; i < NUMBER_RACER; i++)
        {
            if (ms - idListLastMillis[i] > idListTimer[i])
            {
                idListLastMillis[i] = ms;
                idListTimer[i] = random(1500, 3300);
                uint8_t gateNumber_tmp = idGateNumber[i];
                uint8_t gate = (gateNumber_tmp < lastGatesDebug) ? (gateNumber_tmp + 1 ): addressAllGates[0];
                idGateNumber[i] = gate;
                // Serial.print("fake ID: ");
                // Serial.print(idList[i]);
                // Serial.print(" | gate: ");
                // Serial.print(gate);
                // Serial.print(" | millis: ");
                // Serial.println(ms - startMillis);

                // It's the main feature of that function!
                // Simulate a Gate message!
                bufferingID(idList[i], gate, ms - startMillis);
            }
        }
    }
    else
    {
        oldRaceStateDebug = START;
    }

}


// When you have only serial available for racing...
void WriteSerialLive(uint32_t ms, uint8_t protocol){ //, const ID_Data_sorted& data){
  static uint32_t lastMillis = 0;
  const uint32_t delayMillis = 2000;
  char timeChar[10];
  
 if (ms - lastMillis > delayMillis)
 {
    // digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastMillis = millis();
    //  Serial.println(ESP.getFreeHeap()); 237468 237644
 }

  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++){
    if (idData[i].positionChange[protocol] == true || idData[i].needGateUpdate(protocol))
    {
      idData[i].positionChange[protocol] = false;

      for (uint8_t k = 0; k < 15 ; k++){
        Serial.println();
      }
      for (uint8_t j = 1; j < (NUMBER_RACER + 1) ; j++)
      {
        Serial.print(j);
        Serial.print(F(" |ID: "));
        Serial.print(idData[j].ID);
        Serial.print(F(" |laps: "));
        Serial.print(idData[j].laps);
        Serial.print(F(" |Time: "));
        timeToChar(timeChar, 10, idData[j].lastTotalTime);
        Serial.print(timeChar);
        Serial.print(F(" |last: "));
        timeToChar(timeChar, 10, idData[j].lastLapTime);
        Serial.print(timeChar);
        Serial.print(F(" |best: "));
        timeToChar(timeChar, 10, idData[j].bestLapTime);
        Serial.print(timeChar);
        Serial.print(F(" |mean: "));
        timeToChar(timeChar, 10, idData[j].meanLapTime);
        Serial.print(timeChar);

        for ( uint8_t i = 0; i < NUMBER_GATES; i++)
        {
            Serial.print(F(" |"));
            Serial.print(i + 1);
            Serial.print(F(": "));
            // Shift Gate order:
            uint8_t shiftGate = ((i + 1) < NUMBER_GATES) ? (i + 1) : 0;
//            Serial.print(shiftGate);
            timeToChar(timeChar, 10, idData[j].lastCheckPoint[shiftGate]);
            Serial.print(timeChar);
        }
        Serial.println();
      }
      break; // don't flood, only one message at a time!
    }
  }
}


void ReadSerial(){
    if (Serial.available()) {
    char test[128] = "test";
    
    byte inByte = Serial.read();

        switch (inByte) {
        case 'S': // init timer
            raceState = START;
            break;

        case 'T': // End connection
            raceState = STOP;
            break;

        case 'R': // Reset Value
            raceState = RESET;
            break;
       
        case 'P': // Test Message pointer
//            race.setMessage(test);;
            break;

        default:
            break;
        }
    }
}

// Better to process on the mcu c++ or the browser javascript side... ??
// But as there is already an identical function on the javascript side used for current time, remove that one ?
// ie: that function take 80usec to change millis() to human readable time.
void timeToChar(char *buf, int len, uint32_t tmpMillis) { // the len is always 10... "00:00.000"
  unsigned long nowMillis = tmpMillis;
  uint32_t tmp_seconds = nowMillis / 1000;
  uint32_t seconds = tmp_seconds % 60;
  uint16_t ms = nowMillis % 1000;
  uint32_t minutes = tmp_seconds / 60;

  snprintf(buf, len, "%02d:%02d.%03d", minutes, seconds, ms);
}
