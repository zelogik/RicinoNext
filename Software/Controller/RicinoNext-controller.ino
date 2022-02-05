/*
Todo: Add author(s), descriptions, etc here...
*/



// ----------------------------------------------------------------------------
// Imcludes library, header
// ----------------------------------------------------------------------------
// todo: add samd21 (remove any wifi stack), and use an esp01 as simple jsonToSerial -> serialToWeb, shouldn't be hard but low priority.
#if defined(ESP8266)
    #include <ESP8266WiFi.h>  //ESP8266 Core WiFi Library
    #include <ESPAsyncTCP.h>
#else
    #include <WiFi.h>      //ESP32 Core WiFi Library
    #include <AsyncTCP.h>
#endif

// todo: need to check too with esp8266 compatibility..
#include <ESPAsync_WiFiManager.h>
#include <ESPAsyncWebServer.h>
//#include <AsyncElegantOTA.h>;

#include <FS.h> // need to choose!
#include "SPIFFS.h" // need to choose!
#include <SPI.h> // need to choose!

#include <ArduinoJson.h>

#include <Wire.h>     // need for the i2c gate.


// ----------------------------------------------------------------------------
// DEBUG global assigment, add receiver/emitter simulation (put in struct?)
// ----------------------------------------------------------------------------
#define DEBUG 1

#if defined(DEBUG)
  void fakeIDtrigger(int ms); //debug function (replace i2c connection)
  #define JSON_BUFFER_DEBUG 256

#endif

// ----------------------------------------------------------------------------
// AsyncWebServer stuff, JSON stuff
// ----------------------------------------------------------------------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
// DNSServer dns; // as it's local, why use DNS on the server...?
//todo: separate live/race/conf json OR make a dynamic calculation
#define JSON_BUFFER 512
#define JSON_BUFFER_CONF 1024 // need to test with 8 players or more...




// ----------------------------------------------------------------------------
//  External components part. ie: button/tft/esp01 etc...
// ----------------------------------------------------------------------------
uint16_t ledPin = 13;
//#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
//TFT_eSPI tft = TFT_eSPI(135, 240);  // Invoke library, pins defined in User_Setup.h //#define TFT_GREY 0x5AEB // New colour

//#define Button2_USE
#if defined(Button2_USE)
  #include "Button2.h"
  #define BUTTON_1        35
  #define BUTTON_2        0
  Button2 btn1(BUTTON_1);
  Button2 btn2(BUTTON_2);
#endif


// ----------------------------------------------------------------------------
//  Default racer/gates/protocol
// ----------------------------------------------------------------------------
#define NUMBER_RACER 4 // todo/future: set a fixed 32 struct BUT dynamic player number? to avoiding malloc/free etc..
// #define ARRAY_LENGTH(x)  ( sizeof( x ) / sizeof( *x ) )

const uint8_t addressAllGates[] = {21, 22, 23}; // Order: 1 Gate-> ... -> Final Gate! | define address in UI ?
#define NUMBER_GATES ( sizeof( addressAllGates ) / sizeof( *addressAllGates ) )

#define NUMBER_PROTOCOL 3 // 0:serial, 1:web, 2:tft, 3:jsonSerial todo: add enum for easy debug?


// ----------------------------------------------------------------------------
//  Prototype
// ----------------------------------------------------------------------------
void sortIDLoop(); // todo: add to Race class in future


// ----------------------------------------------------------------------------
//  UI Config struct
// That struct is directly changed from client UI.
// Auto send to client when isChanged or at new connection
// ----------------------------------------------------------------------------
struct UI_config{
    bool isChanged = false; // is used? / remove ?

    uint16_t laps = 10; // 1 - ? unlimited ?
    uint8_t players = NUMBER_RACER; // 1 - ? 32 max ? (ESP memory/speed limit)
    uint8_t gates = NUMBER_GATES; // 1…?8 max?

    bool light = false;
    uint8_t light_brightness = 255;

    // Todo Add  enable sound/voice/etc... here
    bool state = false; // trigger a race start/stop
    bool reset = false; // trigger reset struct idData
    bool read_ID = false; // trigger an one shot ID reading

    // is separating this nested struct useful ?
    struct Player
    {
        // uint8_t position; // todo: function to auto find by ID/name
        uint32_t ID; // dedup with idData[i].ID, something to refactoring ?
        char name[16]; // make a "random" username ala reddit ?
        char color[8]; // by char or int/hex value...
    }names[NUMBER_RACER];
};

UI_config uiConfig;


// ----------------------------------------------------------------------------
//  Main Data struct
// One Struct to keep every player data + sorted at each loop.
// ----------------------------------------------------------------------------
struct ID_Data_sorted{
  public:
    // get info from i2c/gate
    uint32_t ID;
    uint32_t lapTime;
    uint8_t currentGate;

    // todo: check the init condition after a start/stop
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
    bool positionChange[NUMBER_PROTOCOL]; // serial, json, serial

    // todo: clean
    int8_t statPos;
    uint8_t lastPos;
    bool haveMissed;
    uint8_t lastGate;
    bool haveInitStart = 0;
    uint8_t indexToRefresh; 


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
        // todo: add only if " < race.numberTotalLaps ?
        laps++;
        // Calculation of full lap!
        lastCalc(&lastLapTime, &lastTotalTime, nullptr, timeMs);
        meanCalc(&meanLapTime, nullptr, timeMs);
        bestCalc(&bestLapTime, lastLapTime);
      }

      // Calculation of checkpoint
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


  // todo, know if up or down or same position.
  void updateRank(uint8_t currentPosition){
    statPos = lastPos - currentPosition;
    lastPos = currentPosition;
  }


  // return true and reset Protocol state
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

        // Example for 3 gates:
        //  0s   4s   7s       10s  13s  16s
        //  |    |    |    ->   |    |    |
    }
};

ID_Data_sorted idData[NUMBER_RACER + 1]; // number + 1, [0] is the tmp for rank change, and so 1st is [1] and not [0] and so on...


// ----------------------------------------------------------------------------
//  Buffer Struct: Sort-Of simple buffer for i2c request from gates
// ----------------------------------------------------------------------------
struct ID_buffer{
    uint32_t ID = 0;
    uint8_t gateNumber = 0;
    uint32_t totalLapsTime = 0; // in millis ?
    bool isNew = false; //
};

ID_buffer idBuffer[NUMBER_RACER];


// ----------------------------------------------------------------------------
//  Race enum, state machine
// ----------------------------------------------------------------------------
enum race_state_t {
    RESET,         // Set parameters(Counter BIP etc...), before start counter, re[set] all struct/class
    WAIT,          // only check gate status, waiting for START call.
    START,         // Run the warm-up phase, (light, beep,etc) (no registering player for the moment)
    RACE,          // enable all gate receiver and so, update Race state (send JSON, sendSerial) etc...
    FINISH,        // 1st player have win, terminate after 20s OR all players have finished.
    STOP           // finished auto/manual goto WAIT
};

race_state_t raceState = RESET;
const char* raceStateChar[] = {"RESET", "WAIT", "START", "RACE", "FINISH", "STOP"}; // todo: Human readable ENUM,used for message ?


// ----------------------------------------------------------------------------
//  Race Class, it's sort-of a main loop.
// ----------------------------------------------------------------------------
class Race {
  private:
    uint16_t delayWarmupDelay = 2 * 1000; // todo: changeable from UI
    uint16_t delayWarmupTimer;
    bool isReady = false;
    uint16_t biggestLap;
    uint16_t numberTotalLaps;
    uint32_t finishRaceMillis;
    uint32_t finishRaceDelay = 2 * 1000; // todo: changeable from UI
    race_state_t oldRaceState;
    const uint8_t messageLength = 128;
    uint32_t startTimeOffset;
    char message[128] = "test Char pointer";


//      // keep for i2c gates
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


    void setMessage(char *test_char[128]){
      memcpy(message, test_char, sizeof(test_char[0])*128);
      // Now set an 
    }


  public:
    Race(){}
    ~Race(){};

    void loop(){

//        printSerialDebug();
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
                numberTotalLaps = uiConfig.laps;
                // setMessage("Warm-UP time");
                memcpy(message, "Warm-UP time", sizeof(message[0])*128);
            }
    
            if (millis() - delayWarmupTimer > delayWarmupDelay)
            {   // i2c gate template
                // for (uint8_t i = 0; i < NUMBER_GATES; i++)
                // {
                //     setCommand(addressAllGates[i], app_ptr->startCmd, sizeof(&app_ptr->startCmd));
                // }
                // app_ptr->startOffset = millis();  //(*ptr).offsetLap;

                startTimeOffset = millis();
                raceState = RACE;
                memcpy(message, "RUN RUN RUN", sizeof(message[0])*128);

            }
            break;

        case RACE:
            #if defined(DEBUG)
                fakeIDtrigger(millis()); //only used for debug
            #endif
            sortIDLoop(); // processing ID.

            // i2c gate template
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
                memcpy(message, "Hurry UP!", sizeof(message[0])*128);
            }

            break;

        case FINISH:
            #if defined(DEBUG)
                fakeIDtrigger(millis()); //only used for debug
            #endif

            sortIDLoop(); // todo: add condition to run only for RACE/FINISH
            // if ALL player > MAX LAP or timeOUT
            // todo: add a all player finished condition
            if (millis() - finishRaceMillis > finishRaceDelay)
            {
                raceState = STOP;
                memcpy(message, "Finished", sizeof(message[0])*128);
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

    char* getMessage(){
      // Send message only one time!
      if (message[0] != '\0')
      {
          char message_tmp[128];
          memcpy(message_tmp, message, sizeof(message[0])*128);
          message[0] = '\0';
          return message_tmp;
      }
      return nullptr;
    }
};

Race race = Race();


// ----------------------------------------------------------------------------
//  Web Stuff: Error function config JSON
// is used somewhere? don't remember!
// ----------------------------------------------------------------------------
void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}


// ----------------------------------------------------------------------------
//  Web Stuff: struct --> json<char[size]>
//  char test[512];
//  confToJSON(&uiConfig, test);
// todo: need a special function to calculate dynamic size
// ----------------------------------------------------------------------------
void confToJSON(char* output){ // const struct UI_config* data,
  StaticJsonDocument<JSON_BUFFER_CONF> doc;

  JsonObject conf = doc.createNestedObject("conf");
  conf["laps"] = uiConfig.laps;
  conf["players"] = uiConfig.players;
  conf["gates"] = uiConfig.gates;
  conf["light"] = uiConfig.light ? 1 : 0;
  conf["light_brightness"] = uiConfig.light_brightness;
  conf["state"] = (raceState > 1 && raceState < 5) ? 1 : 0;

  JsonArray conf_players = conf.createNestedArray("names");
  // todo: change that loop with JsonObject loop
  for ( uint8_t i = 0; i < uiConfig.players ; i++)
  {
      conf_players[i]["id"] = uiConfig.names[i].ID;
      conf_players[i]["name"] = uiConfig.names[i].name;
      conf_players[i]["color"] = uiConfig.names[i].color;
  }

  serializeJsonPretty(doc, output, JSON_BUFFER_CONF);
}


// ----------------------------------------------------------------------------
//  Web Stuff: json<char[size]> --> struct
//  todo: need optimization/factorization
// ----------------------------------------------------------------------------
void JSONToConf(const char* input){ // struct UI_config* data, 
  StaticJsonDocument<JSON_BUFFER_CONF> doc;

  DeserializationError err = deserializeJson(doc, input, JSON_BUFFER_CONF);
  if (err) // todo: instead send error to UI (message)?
  {
      Serial.print(F("deserializeJson() failed with code "));
      Serial.println(err.f_str());
  }

  // todo: Code below need many optimizations!
  JsonObject obj = doc["conf"]; //.as<JsonObject>();

  const char *obj_p = obj["state"];

  // todo: make a JsonObject loop.
  if ( obj_p != nullptr)
  {
      const bool stt = (char)atoi(obj_p);
      raceState = (stt) ? START : STOP;
  }

  // obj_p = obj["laps"];
  if (obj.containsKey("laps"))
  {
      uiConfig.laps = doc["conf"]["laps"];
              // Serial.println(uiConfig.laps);

  }
  if (obj.containsKey("players"))
  {
      uiConfig.players = doc["conf"]["players"];
  }
  if (obj.containsKey("gates"))
  {
      uiConfig.gates = doc["conf"]["gates"];
  }
  if (obj.containsKey("light"))
  {
      uiConfig.light = doc["conf"]["light"];
  }
  if (obj.containsKey("light_brightness"))
  {
      uiConfig.light_brightness = doc["conf"]["light_brightness"];
  }
  // todo: need to find ID/position
  if (obj.containsKey("names"))
  {
      uint8_t count = 0;
      JsonArray plrs = doc["conf"]["names"];
      for (JsonObject plr : plrs) {
          // Serial.print("ID: ");
          // Serial.print(plr["id"].as<long>());
          // Serial.print(" | name: ");
          // Serial.print(plr["name"].as<char *>());
          // Serial.print(" | color: ");
          // Serial.println(plr["color"].as<char *>());
          uiConfig.names[count].ID == plr["id"].as<long>();
          strlcpy(uiConfig.names[count].name, plr["name"] | "", sizeof(uiConfig.names[count].name));
          strlcpy(uiConfig.names[count].color, plr["color"] | "", sizeof(uiConfig.names[count].color));
          count++;
      }
  }
}


// ----------------------------------------------------------------------------
//  Web Stuff: interrupt based function, receive JSON from client
// todo: finish, make it works
// ----------------------------------------------------------------------------
void onEvent(AsyncWebSocket       *server,
             AsyncWebSocketClient *client,
             AwsEventType          type,
             void                 *arg,
             uint8_t              *data,
             size_t                length) {

    char json[JSON_BUFFER_CONF];

    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            confToJSON(json);
            client->text(json);
            break;
    
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA:
        {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && (info->index == 0) && (info->len == length)) {
                if (info->opcode == WS_TEXT)
                {
                    // todo: Add an UI lock/unlock state?
                    JSONToConf((char*)data);

                    confToJSON(json);
                    ws.textAll(json);
                }
                else
                {
                    Serial.println("Received a ws message, but it isn't text");
                }
            }
            else
            {
                Serial.println("Received a ws message, but it didn't fit into one frame");
            }
        //    handleWebSocketMessage(arg, data, len);
            break;
        }
        case WS_EVT_PONG:
            // break;
        case WS_EVT_ERROR:
            break;
    }
}


// ----------------------------------------------------------------------------
//  Web Stuff: merge or separate to onEvent ?
// todo: finish, make it works 
// ----------------------------------------------------------------------------
// void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
//     AwsFrameInfo *info = (AwsFrameInfo*)arg;
//     if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
//         // const char *action = json["action"];
//         // if (strcmp(action, "toggle") == 0) {
//         //     led.on = !led.on;
//         //     notifyClients();
//         // }
//     }
// }


// ----------------------------------------------------------------------------
//  Web Stuff: Only run one time, setup differents web address
// ----------------------------------------------------------------------------
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

  ws.onEvent(onEvent);
  server.addHandler(&ws);
}


// ----------------------------------------------------------------------------
//  Button class: add some physical "touch"
// ----------------------------------------------------------------------------
#if defined(Button2_USE)
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
#endif



// ----------------------------------------------------------------------------
//  Led Class: simple view of ENUM race state
// todo: use the enum raceState ?
// ----------------------------------------------------------------------------
class Led{
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


// ----------------------------------------------------------------------------
//  Setup call...
// todo: cleaning/separate function?
// ----------------------------------------------------------------------------
void setup(void) {
  Serial.begin(9600);

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  ESPAsync_WiFiManager ESPAsync_wifiManager(&server, nullptr , "RicinoNextAP"); // &dns
//  ESPAsync_wifiManager.resetSettings();   //reset saved settings
  ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  ESPAsync_wifiManager.autoConnect("RicinoNextAP");

//  if (WiFi.status() == WL_CONNECTED) { Serial.print(F("Connected. Local IP: ")); Serial.println(WiFi.localIP()); }
//  else { Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status())); }
  // Route to index.html + favicon.ico
  server_init();

  #if defined(Button2_USE)
      button_init();
  #endif

//  AsyncElegantOTA.begin(&server);
//  server.on("/",HTTP_GET, handleRoot);

//  tft.init();
//  tft.setRotation(1);

//   for (uint8_t i = 5; i != 0; i--){
//      tft.setCursor(120 - 15, 70 - 25);
//      tft.setTextSize(5);
//      tft.setTextColor(TFT_RED);
//      tft.fillScreen(TFT_BLACK);
//      tft.print(i);
//       delay(200);
//   }

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


// ----------------------------------------------------------------------------
//  Main loop, keep it small as possible for readability.
// ----------------------------------------------------------------------------
void loop() {

  // btn1.loop();
  // btn2.loop();

  ledState.loop();
  race.loop();

  // add in a class ?
  WriteJSONLive(millis(), 1); // 1 = web
  WriteJSONRace(millis());

  // same here. make a class ?
  // WriteSerialLive(millis(), 0); // 0 = serial
  ReadSerial();

  #if defined(DEBUG)
    writeJSONDebug();
  #endif
}


// ----------------------------------------------------------------------------
// Debug loop, sending good info
// ----------------------------------------------------------------------------
#if defined(DEBUG)
void writeJSONDebug(){
  static uint32_t lastMillis = 0;
  uint32_t delayMillis = 1000;
  static uint32_t worstMicroLoop = 0;
  static uint32_t oldTimeMicroLoop = 0;
  
  // delayMillis = (race.getCurrentTime() == 0) ? 5000 : 1000;
  // calculate the loop time to execute.
  uint32_t nowMicros = micros();
  uint32_t lastTimeMicro = nowMicros - oldTimeMicroLoop;
  oldTimeMicroLoop = nowMicros;

  if (lastTimeMicro > worstMicroLoop )
  {
      worstMicroLoop = lastTimeMicro;
  }

  // send every sec
  if (millis() - lastMillis > delayMillis)
  {
    lastMillis = millis();

    StaticJsonDocument<JSON_BUFFER_DEBUG> doc;
    JsonObject debug_obj = doc.createNestedObject("debug");

    debug_obj["time"] = worstMicroLoop;
    worstMicroLoop = 0;

    char json[JSON_BUFFER_DEBUG];
    serializeJsonPretty(doc, json);
    ws.textAll(json);
  }
}
#endif


// ----------------------------------------------------------------------------
//  Function below need to be/get called at each triggered gate.
// take ID, buffering in a temp struct before to be processed by the sortIDLoop().
// ----------------------------------------------------------------------------
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
              break; // Only registering the first Null .ID found
          }
      }
      break; // Only one ID by message, end the loop faster
    }
  }
}


// ----------------------------------------------------------------------------
//  Function below need to be/get called at each triggered gate.
// take ID, buffering in a temp struct before to be processed by the sortIDLoop().
// The most important loop,need to be the fastest possible (idBuffer overflow? bad sorting? who know...)
// This function checking new data in bufferingID()/idBuffer[i]
// sorting struct/table and process idBuffer -> idData.
// don't know if it's add value to integrating directly in the race private class...
// ----------------------------------------------------------------------------
void sortIDLoop(){

  // check if new data available
  for (uint8_t i = 0; i < NUMBER_RACER ; i++)
  {
    if (idBuffer[i].isNew == true)
    {
       idBuffer[i].isNew = false;
       // look ID at current position
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


// ----------------------------------------------------------------------------
// Send JSON if only if idData have changed, update player statistic
// ----------------------------------------------------------------------------
void WriteJSONLive(uint32_t ms, uint8_t protocol){

  // Send "live" JSON section
  // todo: maybe add a millis delay to don't DDOS client :-D
  // todo: optimization, send only new value! (so need more memory to store lastValue)
  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++){
    if (idData[i].positionChange[protocol] == true || idData[i].needGateUpdate(protocol))
    {
      idData[i].positionChange[protocol] = false;
      StaticJsonDocument<JSON_BUFFER> doc;

      // Need to fill now...
      JsonObject live = doc.createNestedObject("live");

      // todo: need to change to char and timeToChar function!
      live["rank"] = i;
      // add old rank ?
      live["id"] = idData[i].ID;
      live["lap"] = idData[i].laps;
      live["best"] = idData[i].bestLapTime;
      live["last"] = idData[i].lastLapTime;
      live["mean"] = idData[i].meanLapTime;
      live["total"] = idData[i].lastTotalTime;

      // hum... if gate == 1, could we stop here ?
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

// ----------------------------------------------------------------------------
// Send JSON Race state. ie: lap/state/time
// ----------------------------------------------------------------------------
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

    // Need to fill now...
    StaticJsonDocument<JSON_BUFFER> doc;
    JsonObject race_json = doc.createNestedObject("race");

    race_json["state"] = raceStateChar[raceState];
    race_json["lap"] = race.getBiggestLaps();
    race_json["time"] = race.getCurrentTime();
 
    char *valueMessage = race.getMessage();
    if (valueMessage != nullptr)
    {
      //  Serial.println(valueMessage);
       race_json["message"] = valueMessage;
    }

    char json[JSON_BUFFER];
    serializeJsonPretty(doc, json); // todo: remove the pretty after
    ws.textAll(json);
  }
}

// ----------------------------------------------------------------------------
// Debug Loop, simulate the gates
// make it changeable at compile time when i2c will be merged
// ----------------------------------------------------------------------------
#if defined(DEBUG)
void fakeIDtrigger(int ms){

    uint32_t startMillis;
    const uint32_t idList[] = { 0x1234, 0x666, 0x1337, 0x2468, 0x4321, 0x2222, 0x1111, 0x1357};
    uint16_t idListTimer[] = { 2000, 2050, 2250, 2125, 2050, 2150, 2250, 2350}; // used for the first lap!
    uint32_t idListLastMillis[] = { 0, 0, 0, 0, 0, 0, 0, 0,};
    uint8_t idGateNumber[] = { 20, 20, 20, 20, 20, 20, 20, 20}; //  Address of first gate - 1

    static race_state_t oldRaceStateDebug = START;
    static bool isNew = false;
    static uint32_t autoResetReady = millis();
    const uint32_t autoResetReadyDelay = 1000; // 1sec without call this function.
    uint8_t gateNumber_tmp;
    const uint8_t lastGatesDebug = addressAllGates[NUMBER_GATES - 1];

    //detect if it's a new race
    if ( millis() - autoResetReady > autoResetReadyDelay)
    {
        isNew = false;
        Serial.println("AUTO RESET");
    }
    autoResetReady = millis();

    if ( oldRaceStateDebug == START && raceState == RACE && isNew == false)
    {
        isNew = true;
        for (uint8_t i = 0; i < NUMBER_RACER; i++)
        {
            idGateNumber[i] = 20;
            uiConfig.names[i].ID == idList[i];
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
#endif


// ----------------------------------------------------------------------------
// When you have only serial available for racing/debug...
// ----------------------------------------------------------------------------
void WriteSerialLive(uint32_t ms, uint8_t protocol){ //, const ID_Data_sorted& data){
  static uint32_t lastMillis = 0;
  const uint32_t delayMillis = 2000;
  char timeChar[10];
  
 if (ms - lastMillis > delayMillis)
 {
    lastMillis = millis();
    //  Serial.println(ESP.getFreeHeap()); 237468 237644
 }

  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++)
  {
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
            // Shift Gate order:
            uint8_t shiftGate = ((i + 1) < NUMBER_GATES) ? (i + 1) : 0;
            timeToChar(timeChar, 10, idData[j].lastCheckPoint[shiftGate]);
            // Serial.print(timeChar);
        }
        Serial.println();
      }
      break; // don't flood, only one message at a time!
    }
  }
}


// ----------------------------------------------------------------------------
// When you have only serial available for racing/debug...
// ----------------------------------------------------------------------------
void ReadSerial(){
    const char JSONconfDebug[1024] = "{\"conf\":{\"laps\":40,\"players\":4,\"gates\":3,\"light\":0,\"light_brightness\":255,\"state\":1,\"names\":[{\"id\":1,\"name\":\"Player 1\",\"color\":\"blue\"},{\"id\":2,\"name\":\"Player 2\",\"color\":\"red\"},{\"id\":3,\"name\":\"Player 3\",\"color\":\"green\"},{\"id\":4,\"name\":\"Player 4\",\"color\":\"yellow\"}]}}";
    char confJSON[JSON_BUFFER_CONF];


    if (Serial.available()) {
    // char test[128] = "test";
    
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
       
        case 'F': // Test Message
            //char JSONconf[JSON_BUFFER_CONF];
            JSONToConf(JSONconfDebug);

            break;

        case 'B': // Test Message
            confToJSON(confJSON); 
            ws.textAll(confJSON);
            break;

        default:
            break;
        }
    }
}


// ----------------------------------------------------------------------------
// ie: that function take 80usec to change millis() to human readable time.
// Better to process on the mcu c++ or the browser javascript side... ?
// the len is always 10... "00:00.000"
// ----------------------------------------------------------------------------
void timeToChar(char *buf, int len, uint32_t tmpMillis) { 
  unsigned long nowMillis = tmpMillis;
  uint32_t tmp_seconds = nowMillis / 1000;
  uint32_t seconds = tmp_seconds % 60;
  uint16_t ms = nowMillis % 1000;
  uint32_t minutes = tmp_seconds / 60;

  snprintf(buf, len, "%02d:%02d.%03d", minutes, seconds, ms);
}

// Future template to test speed improvement
// uint32_t fastUlongToTimeString(uint64_t secs, char *s)
// {
//   // divide by 3600 to calculate hours
//  uint64_t hours = (secs * 0x91A3) >> 27;
//  uint64_t xrem = secs - (hours * 3600);

//  // divide by 60 to calculate minutes
//  uint64_t mins = (xrem * 0x889) >> 17;
//  xrem = xrem - (mins * 60);

//  s[0] = (char)((hours / 10) + '0');
//  s[1] = (char)((hours % 10) + '0');
//  s[2] = ':';
//  s[3] = (char)((mins / 10) + '0');
//  s[4] = (char)((mins % 10) + '0');
//  s[5] = ':';
//  s[6] = (char)((xrem / 10) + '0');
//  s[7] = (char)((xrem % 10) + '0');

//  return 0;
// }