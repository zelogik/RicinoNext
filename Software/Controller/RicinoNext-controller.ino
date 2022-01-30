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
// bool idChanged = false;
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

#define NUMBER_RACER 4 // backend should work with 4 to endless (memory limit) racer without problem, frontend is fixed to 4 players...
#define ARRAY_LENGTH(x)  ( sizeof( x ) / sizeof( *x ) )

const uint8_t addressAllGates[] = {21, 22, 23}; // Order: 1 Gate-> ... -> Final Gate!
#define NUMBER_GATES ( sizeof( addressAllGates ) / sizeof( *addressAllGates ) )
#define NUMBER_PROTOCOL 3 // 0:serial, 1:ESP, 2:tft


// Physical Button on TTGO Display
#define BUTTON_1            35
#define BUTTON_2            0

Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

uint16_t ledPin = 13;

// prototype
void sortIDLoop();
void fakeIDtrigger(int ms);

/* =============== config section start =============== */
// enum State {
//   DISCONNECTED, // Disconnected, only allow connection
//   CONNECTED, // connected ( start to test alive ping )
//   SET,      // Set RACE init.
//   START, // When START add 20s timeout for RACE
//   RACE, // RACE, update dataRace()...
//   END,  // RACE finish, wait 20sec all players
//   RESET  // RACE finished auto/manual, reset every parameters
// };


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

const char* stateDebugStr[] = {"Reset", "Wait", "Start", "Race", "Finish", "Stop"};


class Race {
  private:
    // race_state_t oldRaceState = raceState;
    uint16_t delayWarmupDelay = 2 * 1000; // need to be dynamic/changeable from UI
    uint16_t delayWarmupTimer;
    bool isReady = false;
    uint16_t biggestLap;
    uint16_t numberTotalLaps = 5;
    uint32_t finishRaceMillis;
    uint32_t finishRaceDelay = 2 * 1000; // 10sec
    race_state_t oldRaceState;
    
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

    void checkLap(){

    }
    void printSerialDebug(){
        if (oldRaceState != raceState)
        {
            Serial.println(stateDebugStr[oldRaceState]);
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

    bool isIdRunning(uint8_t lap){
        return ( lap ==  numberTotalLaps )  ? false : true;
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

//  server.on("/pressure", HTTP_GET, [](AsyncWebServerRequest *request){
//    request->send_P(200, "text/plain", getPressure().c_str());
//  });
//  server.on("/",HTTP_GET, handleRoot);

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

  WriteSerial(millis());

  WriteJSON();

  ReadSerial();
// ReadJSONInterrupt, don't need to be called on each loop.

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
// don't know if it's add value to integrating directly in race class...

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


// completely change this algo!!!
// OUTDATED!!! bad reference etc...
void WriteJSON(){
//// void updateDataLoop(){ //run at each interval OR/AND at each event (updateRacer), that is the question...
//    bool trigger = false;
//    static uint8_t posUpdate = 0;
//    static bool needJsonUpdate = false;
//    static bool needLapUpdate = false;
//    static uint32_t websockCount = 0;
//    const uint16_t websockDelay = 200;
//    static uint32_t websockTimer = millis();
//    static uint32_t websockTimerFast = millis();
//    static uint16_t lastTotalLap = 0;
//    static uint8_t biggestTotalLap = 0;
////    struct racerMemory tmpRacerBuffer;
//    uint32_t tmpRacerBuffer[9] = {0}; // id, numberLap, offsetLap, lastLap, bestLap, meanLap, totalLap
//    const String orderString[] = {"one", "two", "three", "four"}; //change by char...
//    uint16_t tmpTotalLap = 0;
//
//    for (uint8_t i = 0; i < NUM_BUFFER; i++){
//        tmpTotalLap += racerBuffer[i].numberLap;
//        if (racerBuffer[i].numberLap > raceLoop.getBiggestLap()){
//            raceLoop.setBiggestLap(racerBuffer[i].numberLap);
//        }
//
//       if ( i < NUM_BUFFER - 1){
//            if (racerBuffer[i + 1].numberLap > racerBuffer[i].numberLap)
//            { // My worst code ever, but working code... a way to copy a complete struct without got an esp exception...
//                tmpRacerBuffer[0] = racerBuffer[i].id;
//                tmpRacerBuffer[1] = racerBuffer[i].numberLap; 
//                tmpRacerBuffer[2] = racerBuffer[i].offsetLap;
//                tmpRacerBuffer[3] = racerBuffer[i].lastLap;
//                tmpRacerBuffer[4] = racerBuffer[i].bestLap;
//                tmpRacerBuffer[5] = racerBuffer[i].meanLap;
//                tmpRacerBuffer[6] = racerBuffer[i].totalLap;
//                tmpRacerBuffer[7] = racerBuffer[i]._tmpLast;
//                tmpRacerBuffer[8] = racerBuffer[i].color;
//                
//                racerBuffer[i].id = racerBuffer[i + 1].id;
//                racerBuffer[i].numberLap = racerBuffer[i + 1].numberLap;
//                racerBuffer[i].offsetLap = racerBuffer[i + 1].offsetLap;
//                racerBuffer[i].lastLap = racerBuffer[i + 1].lastLap;
//                racerBuffer[i].bestLap = racerBuffer[i + 1].bestLap;
//                racerBuffer[i].meanLap= racerBuffer[i + 1].meanLap;
//                racerBuffer[i].totalLap = racerBuffer[i + 1].totalLap;
//                racerBuffer[i]._tmpLast = racerBuffer[i + 1]._tmpLast;
//                racerBuffer[i].color = racerBuffer[i + 1].color;
//                
//                racerBuffer[i + 1].id = tmpRacerBuffer[0];
//                racerBuffer[i + 1].numberLap = tmpRacerBuffer[1];
//                racerBuffer[i + 1].offsetLap = tmpRacerBuffer[2];
//                racerBuffer[i + 1].lastLap = tmpRacerBuffer[3];
//                racerBuffer[i + 1].bestLap = tmpRacerBuffer[4];
//                racerBuffer[i + 1].meanLap = tmpRacerBuffer[5];
//                racerBuffer[i + 1].totalLap = tmpRacerBuffer[6];
//                racerBuffer[i + 1]._tmpLast = tmpRacerBuffer[7];
//                racerBuffer[i + 1].color = tmpRacerBuffer[8];
//                break;
//            }
//       }
//    }
//    
//    if (tmpTotalLap != lastTotalLap || tmpTotalLap == 0){
//        lastTotalLap = tmpTotalLap;
//        needJsonUpdate = true;
//    }
//    
//    DynamicJsonDocument doc(JSON_BUFFER);
//
//    if( millis() - websockTimerFast > websockDelay){
//        currentRaceMillis = millis() - startRaceMillis;
//        websockTimerFast = millis();
//        doc["websockTimer"] = websockTimer;
//
//        JsonObject data = doc.createNestedObject("data");
//
//        if (millis() - websockTimer > websockDelay / 2 && needJsonUpdate){ //change condition by time to totalLap changed
//            websockTimer = millis();
//            data[orderString[posUpdate] + "_class"] = racerBuffer[posUpdate].color;
//            data[orderString[posUpdate] + "_id"] = racerBuffer[posUpdate].id;
//            data[orderString[posUpdate] + "_lap"] = racerBuffer[posUpdate].numberLap;
//            data[orderString[posUpdate] + "_last"] = millisToMSMs(racerBuffer[posUpdate].lastLap);
//            data[orderString[posUpdate] + "_best"] = millisToMSMs(racerBuffer[posUpdate].bestLap);
//            data[orderString[posUpdate] + "_mean"] = millisToMSMs(racerBuffer[posUpdate].meanLap);
//            data[orderString[posUpdate] + "_total"] = millisToMSMs(racerBuffer[posUpdate].totalLap);
//            data[orderString[posUpdate] + "_current"] = racerBuffer[posUpdate].totalLap; //yes i know...
//            posUpdate++; 
////            if (posUpdate == NUM_BUFFER){
////              posUpdate = 0;
////              needJsonUpdate = false;
////            }
//        }
// 
//        trigger = true;
//    }
//
//    char json[JSON_BUFFER];
//    serializeJsonPretty(doc, json);
//
//    if (trigger){
//        ws.textAll(json);
//    }
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
void WriteSerial(uint32_t ms){ //, const ID_Data_sorted& data){
  static uint32_t lastMillis = 0;
  const uint32_t delayMillis = 2000;
  
 if (ms - lastMillis > delayMillis)
 {
    // digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastMillis = millis();
 }

  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++){
    if (idData[i].positionChange[0] == true || idData[i].needGateUpdate(0))
    {
      idData[i].positionChange[0] = false;

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
        Serial.print(millisToMSMs(idData[j].lastTotalTime));
        Serial.print(F(" |last: "));
        Serial.print(millisToMSMs(idData[j].lastLapTime));
        Serial.print(F(" |best: "));
        Serial.print(millisToMSMs(idData[j].bestLapTime));
        Serial.print(F(" |mean: "));
        Serial.print(millisToMSMs(idData[j].meanLapTime));

        for ( uint8_t i = 0; i < NUMBER_GATES; i++)
        {
            Serial.print(F(" |"));
            Serial.print(i + 1);
            Serial.print(F(": "));
            // Shift Gate order:
            uint8_t shiftGate = ((i + 1) < NUMBER_GATES) ? (i + 1) : 0;
//            Serial.print(shiftGate);
            Serial.print(millisToMSMs(idData[j].lastCheckPoint[shiftGate]));
        }
        Serial.println();

        // Serial.print(F(" |1: "));
        // Serial.print(F(" |2: "));
        // Serial.print(millisToMSMs(idData[j].lastCheckPoint[2]));
        // Serial.print(F(" |3: "));
        // Serial.print(millisToMSMs(idData[j].lastCheckPoint[0]));
      }
      break; // don't flood, only one message at a time!
    }
  }
}


void ReadSerial(){
    if (Serial.available()) {
    
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
       

        default:
            break;
        }
    }
}

// Better to process on the mcu c++ or the browser javascript side... ??
// But as there is already an identical function on the javascript side used for current time, remove that one ?
// todo: replace String to char! as 00:00.000 is well define (memory efficiency)

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

char timeToString(uint32_t tmpMillis){
  
  char str[10] = "";
  unsigned long nowMillis = tmpMillis;
  uint32_t tmp_seconds = nowMillis / 1000;
  uint32_t seconds = tmp_seconds % 60;
  uint16_t ms = nowMillis % 1000;
  uint32_t minutes = tmp_seconds / 60;
  snprintf(str, 10, "%02d:%02d.%03d", minutes, seconds, ms);
  return *str;


    //  timeToString(str, sizeof(str));
    // Serial.println(str); 
}
