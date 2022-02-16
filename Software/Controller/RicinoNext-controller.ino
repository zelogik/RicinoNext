/*
Todo: Add author(s), descriptions, etc here...
*/

#define VSCODIUM_COLOR_HACK 1 // force vscodium color
// todo: https://github.com/me-no-dev/ESPAsyncWebServer#setting-up-the-server, clean OTA, even file browser :)

// ----------------------------------------------------------------------------
// Includes library, header
// ----------------------------------------------------------------------------
#if defined(ESP8266) // todo: check if compile
    #define HAVE_WIFI 1
    #define NUMBER_GATES_MAX 3
    #define NUMBER_RACER_MAX 8
    #include <ESP8266mDNS.h>
    #include <ESP8266WiFi.h>  //ESP8266 Core WiFi Library
    #include <ESPAsyncTCP.h>
    #include <ESPAsync_WiFiManager.h>
    #include <ESPAsyncWebServer.h>
#elif defined(ESP32)
    #define HAVE_OTA 1
    #define HAVE_WIFI 1
    #define NUMBER_RACER_MAX 16
    #define NUMBER_GATES_MAX 7
    #include <Update.h>  // ? #include <ArduinoOTA.h> ?
    #include <ESPmDNS.h>
    #include <WiFi.h>      //ESP32 Core WiFi Library
    #include <AsyncTCP.h>
    #include <FS.h> // Change with littleFS ?
    #include "SPIFFS.h"
    #include <SPI.h> // useless ?
    #include <ESPAsync_WiFiManager.h>
    #include <ESPAsyncWebServer.h>
#elif defined(ARDUINO_SAMD_ZERO) // todo: Doesn't output data as ESP01 is not defined, doesn't compile anymore (AsyncWebsocket)
    // #include <MemoryFree.h>
    #define NUMBER_RACER_MAX 16
    #define NUMBER_GATES_MAX 7
    // set pinLed
    // maybe an simple 328p as ESP01 will take web load.
#elif defined(ARDUINO_AVR_MEGA2560)
    // set pinLed
#else
    #error “Unsupported board selected!”
#endif

#include <Arduino.h>
// #include <EEPROM.h>

#include <ArduinoJson.h>
#define JSON_BUFFER 512
#define JSON_BUFFER_CONF 2500 // 1500 for 8players: need to validate with json/jsonPretty

#include <Wire.h>         // needed for the i2c gates/addons.
#define PACKET_SIZE 13     // I2C packet size, slave will send 14 bytes to master? todo: so why 13...


// ----------------------------------------------------------------------------
// DEBUG:  global assigment, add receiver/emitter simulation (put in struct?),
// DEBUG:  everything below/here will be removed soon or later.
// ----------------------------------------------------------------------------
#define DEBUG 1 //display, output anything debug define
#define DEBUG_FAKE_ID 1 // when you haven't enougth emitter to test multi
#define DEBUG_HARDWARE_LESS // when you haven't receiver/hardware-Free debug

#if defined(DEBUG_HARDWARE_LESS)
    #define DEBUG_FAKE_ID 1
#endif

#if defined(DEBUG_FAKE_ID)
    #define DEBUG 1
    void fakeIDtrigger(int ms); //debug function (replace i2c connection)
#endif

#if defined(DEBUG)
    const char compile_date[] = __DATE__ " " __TIME__;
    char debug_message[128] = {};
    #define JSON_BUFFER_DEBUG 256
    const char JSONconfDebug[2050] = "{\"conf\":{\"laps\":4,\"players\":4,\"gates\":3,\"light\":0,\"state\":0,\"names\":[{\"id\": \"1234\",\"name\":\"Player 1\",\"color\":\"#FFEB3B\"},{\"id\":\"1111\",\"name\":\"Player 2\",\"color\":\"#F44336\"},{\"id\":\"1337\",\"name\":\"Player 3\",\"color\":\"\#03A9F4\"},{\"id\":\"2468\",\"name\":\"Player 4\",\"color\":\"#8BC34A\"},{\"id\":\"6789\",\"name\":\"Player 5\",\"color\":\"#6942468\"},{\"id\":\"8080\",\"name\":\"Player 6\",\"color\":\"#453575\"},{\"id\":\"7878\",\"name\":\"Player 7\",\"color\":\"#4F4536\"},{\"id\":\"8989\",\"name\":\"Player 8\",\"color\":\"#049331\"}]}}";  // ,\"light_brightness\":255

    //example /reminder : stoi(s1)
    // snprintf(s, sizeof(s), "%s is %i years old", name.c_str(), age);
    // strncpy(debug_message, "light :", 128);
    // strncat(debug_message, light_ptr, 128);
    // memcpy(debug_message, compile_date, sizeof(debug_message[0])*128);
    // #define ARRAY_LENGTH(x)  ( sizeof( x ) / sizeof( *x ) )
#endif

struct TIME_debug {
    uint32_t realTime = millis();
    uint32_t startTime;
    uint32_t currentTime;
    uint32_t receiverTime;
    uint32_t irdaTime;
    int32_t delta;
} timeDebug;


// ----------------------------------------------------------------------------
// AsyncWebServer stuff, JSON stuff
// ----------------------------------------------------------------------------
#if defined(HAVE_WIFI)
    AsyncWebServer server(80);
    AsyncWebSocket ws("/ws");
    // AsyncEventSource events("/events"); // Should be a good feature for {race...} AND OTA!! view ESP_AsyncFSBrowser
    // DNSServer dns; // as it's local, why use DNS on the server...?
#endif


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
//  Default racer/gates/protocol, addons address too...
// ----------------------------------------------------------------------------
const uint8_t addressAllGates[] = {21, 22, 23, 24, 25, 26, 27, 28}; // Order: 1 Gate-> ... -> Final Gate! | define address in UI ?
#define NUMBER_GATES ( sizeof( addressAllGates ) / sizeof( *addressAllGates ) )
#define NUMBER_PROTOCOL 4 // 0:serial, 1:web, 2:tft, 3:jsonSerial todo: add enum for easy debug?


// ----------------------------------------------------------------------------
//  Prototypes
// ----------------------------------------------------------------------------
void sortIDLoop(); // todo: add to Race class in future
void setCommand(const uint8_t addressSendGate, const uint8_t *command, const uint8_t commandLength);


// ----------------------------------------------------------------------------
//  Type Race, todo, define more style or simply make a boolean solo
// ----------------------------------------------------------------------------
enum RaceStyle {
    LAPS,       // simple race, until LAP
    TIME        // race until set time
    // PASS,       // when the first overtake/pass the second
    // SOLOLAPS,   // solo mode, only the first ID is detected and show
    // SOLOTIME    // solo mode but limited by laps
};

const char* raceStyleChar[] = {"Laps", "Time", "Pass", "Solo Time", "Solo Laps"};


// ----------------------------------------------------------------------------
//  UI Config struct
// That struct is directly changed from client UI.
// Auto send to client when new connection or when changed
// ----------------------------------------------------------------------------
struct UI_config {
  uint16_t lapsCondition = 10; // 1 - ? unlimited ?
  uint32_t timeCondition = 60 * 1000; // 60sec for debug/test
  uint8_t players = 4; // todo: ? EEPROM ?
  uint8_t gates = 3; // todo: set/get in EEPROM as we don't change setup everytime

  bool state = false; // trigger a race start/stop
  uint8_t light = false;
  bool reset = false; // trigger reset struct idData
  RaceStyle style = LAPS;

  // Give frontend max value accepted
  const uint16_t laps_max = 100;
  const uint32_t time_max = 2 * 60 * 60 * 1000; // time in ms, UI set/get min, 2h for debug.
  const uint8_t players_max = NUMBER_RACER_MAX;
  const uint8_t gates_max = NUMBER_GATES_MAX;

  // todo: below var are not used, find a way to implement
  uint8_t light_brightness = 255;
  bool read_ID = false; // trigger an one shot ID reading
  bool solo = false; // force UI to change page?
  // todo: Add enable sound/voice/etc... here

  // is separating this nested struct useful ?
  struct Player
  {   // uint8_t position; // todo: function to auto find by ID/name ?
      uint32_t id; // dedup with idData[i].ID, something to refactoring ?
      char name[16]; // make a "random" username ala reddit ?
      char color[8]; // by char or int/hex value...
  } names[NUMBER_RACER_MAX];
};

UI_config uiConfig;


// ----------------------------------------------------------------------------
// Main Data struct: One Struct to rules them all. move sortIDloop() from race here.
// ----------------------------------------------------------------------------
struct ID_Data_sorted{
  public:
      // get info from i2c/gate
      uint32_t ID;
      uint32_t lapTime;
      uint8_t currentGate, hit, strength;
      // todo: check the init condition after a start/stop
      // Calculated value:
      uint16_t laps;
      //  uint32_t offsetLap;
      // Make all variable below private ?
      uint32_t bestLapTime, meanLapTime, lastLapTime, lastTotalTime;

      uint32_t lastTotalCheckPoint[NUMBER_GATES_MAX];
      uint32_t lastCheckPoint[NUMBER_GATES_MAX];
      uint32_t bestCheckPoint[NUMBER_GATES_MAX];
      uint32_t meanCheckPoint[NUMBER_GATES_MAX];
      uint32_t sumCheckPoint[NUMBER_GATES_MAX]; // 1193Hours for a buffer overflow?

      bool haveUpdate[NUMBER_PROTOCOL][NUMBER_GATES_MAX];
      bool positionChange[NUMBER_PROTOCOL]; // serial, json, serial

      bool haveInitStart = false;
    //   uint8_t indexToRefresh; 


      // todo: clean
      int8_t statPos;
      uint8_t lastPos;
      bool haveMissed;
      uint8_t lastGate;


  void reset(){
      ID, laps, lapTime, hit, strength = 0;
      lastTotalTime, bestLapTime, meanLapTime, lastLapTime = 0;
      currentGate = addressAllGates[0];
      haveInitStart = false;

      for (uint8_t i = 0; i < NUMBER_GATES_MAX; i++)
      {
          lastTotalCheckPoint[i] = 0;
          lastCheckPoint[i] = 0;
          bestCheckPoint[i] = 0;
          meanCheckPoint[i] = 0;
          sumCheckPoint[i] = 0;
      }
      memset(positionChange, 0, sizeof(positionChange)); // serial, tft, web,... add more
      memset(haveUpdate, 0, sizeof(haveUpdate)); // false?

    //   memset(lastTotalCheckPoint, 0, sizeof(lastTotalCheckPoint));
    //   memset(lastCheckPoint, 0, sizeof(lastCheckPoint));
    //   memset(bestCheckPoint, 0, sizeof(bestCheckPoint));
    //   memset(meanCheckPoint, 0, sizeof(meanCheckPoint));
    //   memset(sumCheckPoint, 0, sizeof(sumCheckPoint));
  }


  void updateTime(uint32_t timeMs, uint8_t gate){
      currentGate = gate;
      lapTime = timeMs; // is used ?
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
          uint8_t prevIndex = (idxGates == 0) ? ((uint8_t)uiConfig.gates - 1) : (idxGates - 1);
      
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
    for (uint8_t i = 0; i < uiConfig.gates; i++)
    {
        if (haveUpdate[protocol][i] == true)
        {
            haveUpdate[protocol][i] = false;
            // indexToRefresh = i;
            boolUpdate = true;
            break; // don't need to check for any other change, will be done next time
        }
    }
    return boolUpdate;
  }


  private:
    uint8_t gateIndex(uint8_t gate){
        uint8_t idx;
        for (uint8_t i = 0; i < uiConfig.gates; i++)
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

ID_Data_sorted idData[NUMBER_RACER_MAX + 1]; // number + 1, [0] is the tmp for rank change, and so 1st is [1] and not [0] and so on...


// ----------------------------------------------------------------------------
//  Buffer Struct: Sort-Of simple buffer for i2c request from gates
// ----------------------------------------------------------------------------
struct ID_buffer{
  uint32_t ID;
  uint8_t gateNumber;
  uint32_t totalLapsTime; // in millis ?
  bool isNew; //
  uint8_t hit;
  uint8_t strength;

  void reset()
  {
    ID, gateNumber, totalLapsTime, hit, strength = 0;
    isNew = false;
  }
};

ID_buffer idBuffer[NUMBER_RACER_MAX];


// ----------------------------------------------------------------------------
//  Race enum, state machine
// ----------------------------------------------------------------------------
enum race_state_t {
  RESET,         // Set parameters(Counter BIP etc...), before start counter, re[set] all struct/class
  WAIT,          // Only check gate status, waiting for START call.
  START,         // Start RACE put in warm-up phase
  WARMUP,        // Run the warm-up phase, (light, beep,etc) (no registering player for the moment)
  RACE,          // Enable all gate receiver and so, update Race state (send JSON, sendSerial) etc...
  FINISH,        // 1st player have win, terminate after 20s OR all players have finished.
  STOP           // Finished auto/manual goto WAIT
};

race_state_t raceState = RESET;
const char* raceStateChar[] = {"RESET", "WAIT", "START", "WARMUP", "RACE", "FINISH", "STOP"}; // todo: Human readable ENUM,used for message ?


// ----------------------------------------------------------------------------
//  Race Class, it's sort-of THE main loop.
// ----------------------------------------------------------------------------
class Race {
  private:
    const uint16_t delayWarmupDelay = 2 * 1000; // todo: changeable from UI
    uint32_t delayWarmupTimer;
    bool isReady = false;
    uint16_t biggestLap;
    const uint16_t *numberTotalLaps = &uiConfig.lapsCondition;
    const uint32_t *numberTotalTime = &uiConfig.timeCondition;
    uint32_t finishRaceMillis;
    const uint32_t finishRaceDelay = 2 * 1000; // todo: changeable from UI
    race_state_t oldRaceState;
    uint32_t startTime;

    // uint32_t currentTimeOffset;
    char message[128] = {};
    char message_buffer[128];
    const uint8_t startCmd[2] = {0x8A}; // I2C start Byte
    const uint8_t stopCmd[2] = {0x8F};  // I2C stop Byte

    RaceStyle *currentStyle = &uiConfig.style;

    uint8_t currentSortPosition;
    uint32_t currentSortTime;
    uint8_t currentSortGate;

    void init() {
        biggestLap = 0;
        isReady = false;
        finishRaceMillis = 0;
        delayWarmupTimer = 0;
        oldRaceState = raceState;
        // startTime = 0;

        uiConfig.reset = false;
        for (uint8_t i = 0 ; i < (NUMBER_RACER_MAX + 1); i++ )
        Serial.print("idData: ")
        {
            Serial.print(i);
            idData[i].reset();
        }
        Serial.print("   | IdBuffer: ")
        for (uint8_t i = 0 ; i < (NUMBER_RACER_MAX); i++ )
        {
            Serial.print(i);
            idBuffer[i].reset();
        }        
    }

    void checkFinal() { //const uint32_t startTime) {
        if (*currentStyle == LAPS) // check if lapsCondition == 
        {
            if (biggestLap == *numberTotalLaps - 1)
            {
                // todo: one time or for each player ?
                memcpy(message, "Final Lap", sizeof(message[0])*128);
            }
            if (biggestLap == *numberTotalLaps)
            {
                finishRaceMillis = millis();
                raceState = FINISH;
                // todo: Only trig one time!
                memcpy(message, "Hurry UP!", sizeof(message[0])*128);
            }
        }
        else if (*currentStyle == TIME) // check if timeCondition - currentTime < 0
        {
            if (millis() - startTime > *numberTotalTime - finishRaceDelay)
            {
                finishRaceMillis = millis();
                raceState = FINISH;
                // todo: Only trig one time!
                memcpy(message, "X second remaining", sizeof(message[0])*128);
            }
        }

    }

    void oneShotStateChange() {
        if (oldRaceState != raceState)
        {
            Serial.println(raceStateChar[oldRaceState]);
            oldRaceState = raceState;
        }
    }

    void setMessage(char *test_char[128]) {
      memcpy(message, test_char, sizeof(message[0])*128);
      // Now set an 
    }

    // ----------------------------------------------------------------------------
    //  Function below need to be/get called at each triggered gate.
    // take ID, buffering in a temp struct before to be processed by the sortIDLoop().
    // The most important loop,need to be the fastest possible (idBuffer overflow? bad sorting? who know...)
    // This function checking new data in bufferingID()/idBuffer[i]
    // sorting struct/table and process idBuffer -> idData.
    // move in the idData struc, could add readability
    // ----------------------------------------------------------------------------
    void sortIDLoop() {
      // check if new data available
        for (uint8_t i = 0; i < uiConfig.players ; i++)
        {
            if (idBuffer[i].isNew == true)
            {
                idBuffer[i].isNew = false;
                // look ID at current position
                currentSortPosition = 0;
                currentSortTime = idBuffer[i].totalLapsTime;
                currentSortGate = idBuffer[i].gateNumber;

                // get current position
                for (uint8_t j = 1; j < uiConfig.players + 1 ; j++)
                {
                    if ( idBuffer[i].ID == idData[j].ID )
                    {
                        currentSortPosition = j;
                        if (isIdRunning(idData[currentSortPosition].laps))
                        {
                            idData[currentSortPosition].updateTime(currentSortTime, currentSortGate);
                        }
                        // update biggestLap.
                        Serial.print("current:  ");
                        Serial.print(currentSortPosition);
                        Serial.print("  | laps: ");
                        Serial.println(idData[currentSortPosition].laps);
                        setBiggestLap(idData[currentSortPosition].laps);

                        break; // Processing only one idBuffer at a time!
                    }
                }
                //  check if ID get up in position/rank.
                for (uint8_t k = currentSortPosition; k > 1 ; k--)
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


  public:
    Race(){}
    ~Race(){};

    void loop(){
        sortIDLoop();
        
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
            init();
            delayWarmupTimer = millis();
            raceState = WARMUP;
            Serial.println("Warm-up!");
            // isReady = true;
            memcpy(message, "Warm-UP time", sizeof(message[0])*128);
            break;

        case WARMUP:
            #if defined(DEBUG_FAKE_ID)
                fakeIDtrigger(millis()); //only used for debug
            #endif

            if (millis() - delayWarmupTimer > delayWarmupDelay)
            {   
                startTime = millis();
                timeDebug.startTime = millis(); // debug!!!!!!!!!!!!!!
                raceState = RACE;
                setCommand(21, startCmd, sizeof(startCmd[0]));
                memcpy(message, "RUN RUN RUN", sizeof(message[0])*128);
            }
            break;

        case RACE:
            #if defined(DEBUG_FAKE_ID)
                fakeIDtrigger(millis()); //only used for debug
            #endif
            // sortIDLoop(); // processing ID.

            checkFinal();
            break;

        case FINISH:
            #if defined(DEBUG_FAKE_ID)
                fakeIDtrigger(millis()); //only used for debug
            #endif

            // sortIDLoop(); // todo: add condition to run only for RACE/FINISH
            // if ALL player > MAX LAP or timeOUT
            // todo: add a all player finished condition
            if (millis() - finishRaceMillis > finishRaceDelay)
            {
                raceState = STOP;
                memcpy(message, "Finished", sizeof(message[0])*128);
            }
            break;

        case STOP:
            // isReady = false;
            raceState = WAIT;
            setCommand(21, stopCmd, sizeof(stopCmd[0]));
            break;
        
        default:
            break;
        }
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
            return ( millis() - startTime );
        }
        else
        {
            return 0;
        }
    }

    bool isIdRunning(uint8_t lap){
        return ( lap == *numberTotalLaps )  ? false : true;
    }

    char* getMessage(){
      // Send message only one time!
      // could be better !?!
        if (oldRaceState != raceState)
        {
            if (message[0] != '\0')
            {
                memcpy(message_buffer, message, sizeof(message[0])*128);
                message[0] = '\0';
                return message_buffer;
            }
            // Serial.println(raceStateChar[oldRaceState]);
            oldRaceState = raceState;
        }
        return nullptr;
    }
};

Race race = Race();


// ----------------------------------------------------------------------------
//  Struct of Gates information
// ----------------------------------------------------------------------------
struct GATE_Data {
    uint8_t address;
    uint32_t currentTime;
    uint8_t receiverState; // CONNECTED/START/RACE/STOP
    uint8_t deltaOffsetGate; //0-255 to 0-100%
    uint8_t bytesInBuffer; //typo wait
    uint8_t loopTime; //wait
};

GATE_Data gateData[NUMBER_GATES_MAX]; //NUMBER_GATES];


// ----------------------------------------------------------------------------
//  Led Class: simple view of ENUM race state
// todo: ala ricinoNext-receiver, check the raceState Enum status....
// ----------------------------------------------------------------------------
class Led{
private:
    uint16_t _ledPin;
    uint32_t OnTime = 1000;     // milliseconds of on-time
    uint32_t OffTime = 1000;    // milliseconds of off-time
    bool ledState = LOW;        // ledState used to set the LED
    uint32_t previousMillis;    // will store last time LED was updated
 
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
//  Web Stuff: Error function config JSON, todo: is used somewhere? don't remember!
// ----------------------------------------------------------------------------
// void notFound(AsyncWebServerRequest* request) {
//     request->send(404, "text/plain", "Not found");
// }

// ----------------------------------------------------------------------------
//  Web Stuff: struct --> json<char[size]>
// todo: need a special function to calculate dynamic size
// ----------------------------------------------------------------------------
#if defined(HAVE_WIFI)
void confToJSON(AsyncWebSocketClient * client) {  //char* output, bool connection){ // const struct UI_config* data,
  DynamicJsonDocument doc(JSON_BUFFER_CONF);

  JsonObject conf = doc.createNestedObject("conf");
  conf["laps"] = uiConfig.lapsCondition;
  conf["time"] = uiConfig.timeCondition;
  conf["players"] = uiConfig.players;
  conf["gates"] = uiConfig.gates;
  conf["light"] = uiConfig.light;
  conf["reset"] = uiConfig.reset;
  conf["style"] = uiConfig.style; // raceStyleChar[uiConfig.style];

  // conf["light_brightness"] = uiConfig.light_brightness;
  conf["state"] = (raceState > 1 && raceState < 5) ? 1 : 0;

  JsonArray conf_players = conf.createNestedArray("names");
  // todo: change that loop with JsonObject loop
  for ( uint8_t i = 0; i < uiConfig.players ; i++)
  {
      conf_players[i]["id"] = uiConfig.names[i].id;
      conf_players[i]["name"] = uiConfig.names[i].name;
      conf_players[i]["color"] = uiConfig.names[i].color;
  }

  if (client)
  {
      conf["laps_m"] = uiConfig.laps_max;
      conf["time_m"] = (uiConfig.time_max / (60 * 1000));
      conf["players_m"] = uiConfig.players_max;
      conf["gates_m"] = uiConfig.gates_max;      

      JsonObject race_obj = doc.createNestedObject("race");
      race_obj["message"] = "Welcome on RicinoNext";

      #if defined(DEBUG)
      JsonObject debug = doc.createNestedObject("debug");
      debug["message"] = compile_date;
      #endif
  }

  #if defined(DEBUG)
  size_t len = measureJsonPretty(doc);
  #else
  size_t len = measureJson(doc);
  #endif

  AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
  if (buffer)
  {
        #if defined(DEBUG)
        serializeJsonPretty(doc, (char *)buffer->get(), len + 1); // len + 1);
        #else
        serializeJson(doc, (char *)buffer->get(), len + 1);
        #endif
        // root.printTo((char *)buffer->get(), len + 1);
        if (client)
        {
            client->text(buffer);
        }
        else
        {  // todo: add an antiflood function! (ie: send message every 1s for example)
            ws.textAll(buffer);
        }
  }
}
#endif

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

    // todo: Code below need many optimizations/factotization!, but working now...
    JsonObject obj = doc["conf"]; //.as<JsonObject>();

    const char *state_ptr = obj["state"];
    const char *light_ptr = obj["light"];
    const char *reset_ptr = obj["reset"];

    // below is sort-of trigger button
    // todo: make a JsonObject loop.
    if ( state_ptr != nullptr)
    {
        const bool stt = (char)atoi(state_ptr);
        raceState = (stt) ? START : STOP;
    }

    if ( reset_ptr != nullptr)
    {
    //   const bool stt = (char)atoi(reset_ptr);
        raceState = RESET; // trigger backend reset
    //   const bool stt = (char)atoi(reset_ptr);
        uiConfig.reset = true; // broadcast trigger!
    }

    if (light_ptr != nullptr)
    {
        uiConfig.light = atoi(light_ptr); //doc["conf"]["light"];
    }

    // todo: make a readable loop...
    // below is number
    // obj_p = obj["laps"];
    if (obj.containsKey("laps"))
    {
        uiConfig.lapsCondition = (doc["conf"]["laps"] > uiConfig.laps_max) ? uiConfig.laps_max : doc["conf"]["laps"];
    }
    if (obj.containsKey("time"))
    {
        uiConfig.timeCondition = (doc["conf"]["time"] > uiConfig.time_max) ? uiConfig.time_max : doc["conf"]["time"];
    }
    if (obj.containsKey("players"))
    {
        uiConfig.players = (doc["conf"]["players"] > uiConfig.players_max) ? uiConfig.players_max : doc["conf"]["players"];
    }
    if (obj.containsKey("gates"))
    {
        uiConfig.gates = (doc["conf"]["gates"] > uiConfig.gates_max) ? uiConfig.gates_max : doc["conf"]["gates"];
    }

    if (obj.containsKey("style"))
    {
        uiConfig.style = doc["conf"]["style"];
    }

    // if (obj.containsKey("light_brightness"))
    // {
    //     uiConfig.light_brightness = doc["conf"]["light_brightness"];
    // }
    // todo: need to find ID/position?
    if (obj.containsKey("names"))
    {
        uint8_t count = 0;
        JsonArray plrs = doc["conf"]["names"];
        for (JsonObject plr : plrs) {
        //sprintf(str,"%d",value) converts to decimal base.
            uiConfig.names[count].id = plr["id"].as<long>();
            strlcpy(uiConfig.names[count].name, plr["name"] | "", sizeof(uiConfig.names[count].name));
            strlcpy(uiConfig.names[count].color, plr["color"] | "", sizeof(uiConfig.names[count].color));
            count++;
        }
    }
}


// ----------------------------------------------------------------------------
//  Web Stuff: interrupt based function, receive JSON from client and/or OTA
// ----------------------------------------------------------------------------
#if defined(HAVE_WIFI)
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
            confToJSON(client);
            // client->text(json);
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
                    confToJSON(nullptr); // then broadcast
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
            Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), length, (length)?(char*)data:"");
            break;

        case WS_EVT_ERROR:
            Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
            break;
    }
}
#endif


// ----------------------------------------------------------------------------
//  Web Stuff: Only run one time, setup differents web address
//  todo: clean OTA, even if that feature works well as it.
// ----------------------------------------------------------------------------
#if defined(HAVE_WIFI)
void server_init()
{
    server.serveStatic("/css/", SPIFFS, "/css/");
    server.serveStatic("/js/", SPIFFS, "/js/");
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404);
    });

    // server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    //     request->send(SPIFFS, "/index.html", "text/html");
    // });

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/favicon.ico", "image/ico");
    });

    server.on("/update", HTTP_GET, [&](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/update.html", "text/html");
    });


    server.on("/update", HTTP_POST, [&](AsyncWebServerRequest *request){
                    AsyncWebServerResponse *response = request->beginResponse((Update.hasError())?500:200, "text/plain", (Update.hasError())?"FAIL":"OK");
                    response->addHeader("Connection", "close");
                    response->addHeader("Access-Control-Allow-Origin", "*");
                    request->send(response);
                    yield();
                    ESP.restart();
    }, [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            // #if defined(ESP8266)
            //     int cmd = (filename == "filesystem") ? U_FS : U_FLASH;
            //     Update.runAsync(true);
            //     size_t fsSize = ((size_t) &_FS_end - (size_t) &_FS_start);
            //     uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            //     if (!Update.begin((cmd == U_FS)?fsSize:maxSketchSpace, cmd)){ // Start with max available size
            // #elif defined(ESP32)
            int cmd = (filename == "filesystem") ? U_SPIFFS : U_FLASH;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd))
            { // Start with max available size
                // #endif
                //     Update.printError(Serial);
                //     return request->send(400, "text/plain", "OTA could not begin");
                // }
            }

            // Write chunked data to the free sketch space
            if(len){
                if (Update.write(data, len) != len)
                {
                    return request->send(400, "text/plain", "OTA could not begin");
                }
            }
                
            if (final) { // if the final flag is set then this is the last frame of data
                if (!Update.end(true))
                { //true to set the size to the current progress
                    Update.printError(Serial);
                    return request->send(400, "text/plain", "Could not end OTA");
                }
            }else{
                return;
            }
    });

  server.begin();
  yield();
  ws.onEvent(onEvent);
  yield();
  server.addHandler(&ws);
}
#endif


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
//  Setup call...
// ----------------------------------------------------------------------------
void setup(void) {
    Serial.begin(9600);

    #if defined(HAVE_WIFI)
    if(!SPIFFS.begin()){
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    //   ESPAsync_wifiManager.resetSettings();   //reset saved settings
    ESPAsync_WiFiManager ESPAsync_wifiManager(&server, nullptr , "RicinoNextAP"); // &dns
    ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    ESPAsync_wifiManager.autoConnect("ricinoNextAP");

    server_init();

    if (!MDNS.begin("ricinoNext"))
    {
        Serial.println("Error setting up MDNS responder!");
    }

        #if defined(DEBUG)
            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.print(F("Connected. Local IP: "));
                Serial.println(WiFi.localIP());
            }
            else
            {
                Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status()));
            }

            JSONToConf(JSONconfDebug);
        #endif
    #endif

    #if defined(Button2_USE)
        button_init();
    #endif

    memcpy(debug_message, compile_date, sizeof(debug_message[0])*128);

    Wire.begin();

    // //reminder for use both ESP32 core!
    // todo: define what on core0 and what on core1, priority task etc...
    // like led class is very low priority
    // race class, and get data from i2c is VERY high priority
    // web things is normal priority
    // finalize order/assign core etc....
    //   xTaskCreatePinnedToCore(
    //                     Task1code,   /* Task function. */
    //                     "Task1",     /* name of task. */
    //                     10000,       /* Stack size of task */
    //                     NULL,        /* parameter of the task */
    //                     1,           /* priority of the task */
    //                     &Task1,      /* Task handle to keep track of created task */
    //                     0);          /* pin task to core 0 */    

    // Void Task1code( void * parameter) {
    //   for(;;) {
    //     Code for task 1 - infinite loop
    //     (...)
    //   }
    // }

    // todo:
    // Add EEPROM or SPIFFS hardware conf (number gate, addons, screen)
    // OR let setup at compile time... or FS.conf override value at compile time... humhum
}


// ----------------------------------------------------------------------------
//  Main loop, keep it small as possible for readability.
// ----------------------------------------------------------------------------
void loop() {
    #if defined(Button2_USE)
    btn1.loop();
    btn2.loop();
    #endif

    ledState.loop();
    race.loop();

    // add in a class ? Time to learn/test inherance.
    WriteJSONLive(millis(), 1); // 1 = web
    WriteJSONRace(millis());

    // same here. make a class ?
    ReadSerial();

    #if defined(DEBUG)
    writeJSONDebug();
    WriteSerialLive(millis(), 0); // 0 = serial
    #endif

    requestGate();

    // ws.cleanupClients();
}


// ----------------------------------------------------------------------------
// Function below need to be/get called at each triggered gate.
// take ID, buffering in a temp struct before to be processed by the sortIDLoop().
// ----------------------------------------------------------------------------
void bufferingID(uint32_t ID, uint8_t gate, uint32_t totalTime, uint8_t hit, uint32_t strength) {

  for (uint8_t i = 0; i < uiConfig.players; i++)
  {
    if (idBuffer[i].ID == ID)
    {
        idBuffer[i].totalLapsTime = totalTime;
        idBuffer[i].gateNumber = gate;
        idBuffer[i].hit = hit;
        idBuffer[i].strength = strength;
        idBuffer[i].isNew = true;
        break; // Only one ID by message, end the loop faster
    }
    else if (idBuffer[i].ID == 0)
    {
        idBuffer[i].ID = ID;
        idBuffer[i].totalLapsTime = totalTime;
        idBuffer[i].gateNumber = gate;
        idBuffer[i].hit = hit;
        idBuffer[i].strength = strength;
        idBuffer[i].isNew = true;
        for (uint8_t j = 1; j < uiConfig.players + 1; j++)
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
// Debug loop, sending useful info
// ----------------------------------------------------------------------------
#if defined(DEBUG)
void writeJSONDebug(){
  static uint32_t lastMillis = 0;
  const uint16_t delayMillis = 10 * 1000;
  static uint32_t worstMicroLoop = 0;
  static uint32_t oldTimeMicroLoop = 0;
  
  // calculate the loop time to execute.
  uint32_t nowMicros = micros();
  uint32_t lastTimeMicro = nowMicros - oldTimeMicroLoop;
  oldTimeMicroLoop = nowMicros;

  if (lastTimeMicro > worstMicroLoop )
  {
      worstMicroLoop = lastTimeMicro;
  }

  // send every sec
  if (millis() - lastMillis > delayMillis || debug_message[0] != '\0')
  {
      lastMillis = millis();

      StaticJsonDocument<JSON_BUFFER_DEBUG> doc;
      JsonObject debug_obj = doc.createNestedObject("debug");

      debug_obj["time"] = worstMicroLoop;
      worstMicroLoop = 0;

      #if defined(ESP32) || defined(ESP8266) 
      char freeMemory[16];
      sprintf(freeMemory,"%lu", ESP.getFreeHeap());
      debug_obj["memory"] = freeMemory;
    //   #else
    //   freeMemory()
      #endif

      if (debug_message[0] != '\0')
      {
          // memcpy(debug_message, message, sizeof(message[0])*128);
          debug_obj["message"] = debug_message;
          debug_message[0] = '\0';
      }

      debug_obj["currentTime"] = gateData[0].currentTime;
      debug_obj["state"] = gateData[0].receiverState;
      debug_obj["deltaOffsetGate"] = gateData[0].deltaOffsetGate;
      debug_obj["bytesInBuffer"] = gateData[0].bytesInBuffer;
      debug_obj["loopTime"] = gateData[0].loopTime;
      
      char color[8];
      getRandomColor(color, 8);
      debug_obj["color"] = color;

      char json[JSON_BUFFER_DEBUG];
      #if defined(DEBUG)
      serializeJsonPretty(doc, json);
      #else
      serializeJson(doc, json);
      #endif

      #if defined(HAVE_WIFI)
      ws.textAll(json);
      #endif
  }
}
#endif


// ----------------------------------------------------------------------------
// Send JSON only if idData have changed, update player stats/position
// todo: maybe add a millis delay to don't DDOS client :-D
// todo: optimization, send only new changed value! (so need more memory to store lastValue)
// ----------------------------------------------------------------------------
void WriteJSONLive(uint32_t ms, uint8_t protocol){
  for (uint8_t i = 1; i < (uiConfig.players + 1) ; i++){
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
        for ( uint8_t i = 0; i < uiConfig.gates; i++)
        {
            uint8_t shiftGate = ((i + 1) < uiConfig.gates) ? (i + 1) : 0;
            live_gate.add(idData[i].lastCheckPoint[shiftGate]);
        }

        char json[JSON_BUFFER];
        #if defined(DEBUG)
        serializeJsonPretty(doc, json);
        #else
        serializeJson(doc, json);
        #endif
        
        #if defined(HAVE_WIFI)
        ws.textAll(json);
        #endif

        break;
    }
  }
}


// ----------------------------------------------------------------------------
// Send JSON Race state at set frequency and/or change. ie: lap/state/time
// ----------------------------------------------------------------------------
void WriteJSONRace(uint32_t ms){
  static race_state_t oldRaceState = raceState;
  static uint32_t lastMillis = 0;
  uint32_t delayMillis;
  static bool isTrig = false;

  delayMillis = (raceState >= START && raceState <= FINISH) ? 1000 : 5000;

  if (oldRaceState != raceState)
  {
      isTrig = true;
  }

  if (ms - lastMillis >= delayMillis || isTrig)
  {
        if (isTrig) // Triggered by change raceState
        {
            isTrig = false;
        }
        else // Triggered by delayMillis
        {
            if (ms - lastMillis >= delayMillis + 1000)
            {
                lastMillis = ms; // init
            }
            else
            {
                lastMillis = lastMillis + delayMillis;
            }
        }

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
        #if defined(DEBUG)
        serializeJsonPretty(doc, json);
        #else
        serializeJson(doc, json);
        #endif

        #if defined(HAVE_WIFI)      
        ws.textAll(json);
        #endif
  }

  oldRaceState = raceState;
}


// ----------------------------------------------------------------------------
// Debug Loop, simulate gates, is a program itself...
// make it changeable at compile time when i2c will be merged
// ----------------------------------------------------------------------------
#if defined(DEBUG_FAKE_ID)
void fakeIDtrigger(int ms){

    static uint32_t startMillis;
    // todo: make a random generator... ?
    const uint32_t idList[16] = { 1234, 1111, 1337, 2468, 6789, 8080, 7878, 8989, 9999, 2222, 9753, 1357, 0403, 2510, 4333, 5000};
    static uint16_t idListTimer[16] = { 2000, 2080, 2250, 2125, 2050, 2150, 2250, 2350, 2005, 2055, 2255, 2120, 2055, 2155, 2255, 2355}; // used for the first lap!
    static uint32_t idListLastMillis[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static uint8_t idGateNumber[16] = { 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20}; //  Address of first gate - 1

    static race_state_t oldRaceStateDebug = START;
    static bool isNew = false;
    static uint32_t autoResetReady = millis();
    const uint32_t autoResetReadyDelay = 1000; // 1sec without call this function.
    uint8_t gateNumber_tmp;
    const uint8_t lastGatesDebug = addressAllGates[uiConfig.gates - 1];

    static uint32_t lastMessageTime = millis();
    const uint32_t lastMessageDelay = 30; // 1sec without call this function.

    //detect if it's a new race
    if ( ms - autoResetReady > autoResetReadyDelay)
    {
        isNew = false;
    //    Serial.println("AUTO RESET");
    }
    autoResetReady = millis();

    if (oldRaceStateDebug == START && raceState == WARMUP && isNew == false)
    { // reset condition
        isNew = true;
        for (uint8_t i = 0; i < uiConfig.players; i++)
        {
            idGateNumber[i] = 20;
            uiConfig.names[i].id = idList[i];
            idListTimer[i] = random(2000, 3000);
            idListLastMillis[i] = 0;
        }
        oldRaceStateDebug = WARMUP;
        startMillis = millis() + 2000; //warmup time?
        // Serial.println("NEW");
    }

    if (ms - lastMessageTime > lastMessageDelay)
    { // send message without DDOS
        lastMessageTime = ms;

        if (isNew)
        {
            for (uint8_t i = 0; i < uiConfig.players; i++)
            {
                if (ms - idListLastMillis[i] > idListTimer[i])
                {
                    idListLastMillis[i] = ms;
                    idListTimer[i] = random(1000, 4000);
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
                    // Simulate a Gate/receiver message! like an emitter have triggered a gate
                    #if defined(DEBUG_HARDWARE_LESS)
                    bufferingID(idList[i], gate, ms - startMillis, 200, 200);
                    #else //send to real i2c receiver!
                    // make a 13byte fake ID ala robi and send to our gate!
                    uint8_t toSend[13] = {};
                    uint32_t startMs = ms - startMillis;
                    //uint8_t (*bytes)[4] = (void *) &value;
                    toSend[0] = 0x13;
                    toSend[1] = gate; // use fake gate instead checksum
                    toSend[2] = 0x84;
                    toSend[3] = (uint32_t)(idList[i] & 0xff);
                    toSend[4] = (uint32_t)(idList[i] >> 8) & 0xff;
                    toSend[5] = (uint32_t)(idList[i] >> 16) & 0xff;
                    toSend[6] = (uint32_t)(idList[i] >> 24);
                    toSend[7] = (uint32_t)(startMs & 0xff);
                    toSend[8] = (uint32_t)(startMs >> 8) & 0xff;
                    toSend[9] = (uint32_t)(startMs >> 24);
                    toSend[10] = (uint32_t)(startMs >> 24);
                    toSend[11] = random(1, 255);
                    toSend[12] = random(1, 255);
                    setCommand(21, toSend, 13); //only gate 21 connected...
                    #endif
                }
            }
        }
        else
        {
            oldRaceStateDebug = START;
        }
    }
}
#endif


// ----------------------------------------------------------------------------
// Check if Gate have new data to processing
// - Pong: 0x82 | ReceiverAddress | Checksum | State: 1= CONNECTED, 3= RACE... | last delta between RobiTime and offsetTime  0.001s/bit | Serial Buffer percent 0-255 | Loop Time in 1/10 of ms.
// - ID:   0x84 | ReceiverAddress | Checksum | ID 4bytes (reversed) | TIME 4bytes (reversed) | signal Strenght | Signal Hit
// - Gate: 0x83 | ReceiverAddress | Checksum | TIME 4bytes (reversed)
// ----------------------------------------------------------------------------
void requestGate() {
    static uint32_t gateRequestTimer = millis();
    const uint32_t gateRequestDelay = 20;
    static uint8_t gateCounter = 0;

    if (millis() - gateRequestTimer >= gateRequestDelay)
    {
        gateRequestTimer = millis();
        // bool needProcessData;
        uint8_t I2C_Packet[PACKET_SIZE];
        // uint32_t stupidI2CBugByte = (uint32_t)addressRequestGate;
        
        Wire.requestFrom((int)addressAllGates[gateCounter], PACKET_SIZE);

        // Find a way to stop the i2c the fastest way possible if receiver send less Bytes.
        uint8_t countPosArray = 0;
        while(Wire.available())
        {
            I2C_Packet[countPosArray] = Wire.read();
            countPosArray++;
        }

        // If we got an I2C packet, we can extract the values
        switch (I2C_Packet[0])
        {
        case 0x83: //get timeStamp data
            // printI2CDebug(I2C_Packet, 13);
            processGateData(I2C_Packet);
            break;

        case 0x84: // get ID data
            // printI2CDebug(I2C_Packet, 13);
            processIDData(I2C_Packet);
            break;

        case 0x82: // error code/ info emitter
            // printI2CDebug(I2C_Packet, 13);
            processReceiverData(I2C_Packet);
            break;

        default:
            break;
        }

        gateCounter++;
        if (gateCounter == uiConfig.gates)
        {
            gateCounter = 0;
        }
    }
}


// DEBUG FUNCTION!, could,should be removed...
void printI2CDebug(const uint8_t *data, const uint8_t len)
{

    bool noReceiverInfo = true;
    uint32_t timeMs;
    uint32_t idRec;
    static bool wasRealData = false;

    if (data[0] == 0x83)
    {
        if (!wasRealData)
        {
            Serial.println();
        }
        Serial.print("timeStamp:  ");
        timeMs = ( ((uint32_t)data[6] << 24)
                 + ((uint32_t)data[5] << 16)
                 + ((uint32_t)data[4] <<  8)
                 + ((uint32_t)data[3] ));
        Serial.print(timeMs, DEC);
        Serial.print(" | ");
    }
    else if (data[0] == 0x84)
    {
        if (!wasRealData)
        {
            Serial.println();
        }
        Serial.print("ID:  ");

        timeMs = ( ((uint32_t)data[10] << 24)
                + ((uint32_t)data[9] << 16)
                + ((uint32_t)data[8] <<  8)
                + ((uint32_t)data[7] ) );
        idRec = ( ((uint32_t)data[6] << 24)
                + ((uint32_t)data[5] << 16)
                + ((uint32_t)data[4] <<  8)
                + ((uint32_t)data[3] ) );
        Serial.print(idRec, DEC);
        Serial.print(" | Gate: ");
        Serial.print(data[1], DEC);
        Serial.print(" | Time: ");
        Serial.print(timeMs, DEC);
        Serial.print(" | ");
    }
    else
    {
        if (wasRealData)
        {
            wasRealData = false;
        }
        // Serial.print(".");
        noReceiverInfo = false;
    }

    if (noReceiverInfo)
    {
        for (uint8_t i = 0; i < len; i++)
        {
            Serial.print(data[i], HEX);
            Serial.print(" ");
        }
        wasRealData = true;
    }

    if (wasRealData)
    {
        Serial.println();
    }

}


// ----------------------------------------------------------------------------
// When you have only serial available for racing/debug...
// ----------------------------------------------------------------------------
void WriteSerialLive(uint32_t ms, uint8_t protocol){ //, const ID_Data_sorted& data){
  static uint32_t lastMillis = 0;
  const uint32_t delayMillis = 2000;
  char timeChar[10]; // 00:00.000
  
 if (ms - lastMillis > delayMillis)
 {
    lastMillis = millis();
 }

  for (uint8_t i = 1; i < (uiConfig.players + 1) ; i++)
  {
      if (idData[i].positionChange[protocol] == true || idData[i].needGateUpdate(protocol))
      {
          idData[i].positionChange[protocol] = false;

          for (uint8_t k = 0; k < 15 ; k++){
              Serial.println();
          }
          for (uint8_t j = 1; j < (uiConfig.players + 1) ; j++)
          {
              Serial.print(j);
              Serial.print(F(" |ID: "));
              Serial.print(idData[j].ID);
              Serial.print(F(" |laps: "));
              Serial.print(idData[j].laps);
              Serial.print(F(" |Time: "));
              timeToChar(timeChar, idData[j].lastTotalTime);
              Serial.print(timeChar);
              Serial.print(F(" |last: "));
              timeToChar(timeChar, idData[j].lastLapTime);
              Serial.print(timeChar);
              Serial.print(F(" |best: "));
              timeToChar(timeChar, idData[j].bestLapTime);
              Serial.print(timeChar);
              Serial.print(F(" |mean: "));
              timeToChar(timeChar, idData[j].meanLapTime);
              Serial.print(timeChar);

              for ( uint8_t i = 0; i < uiConfig.gates; i++)
              {
                  // Shift Gate order:
                  uint8_t shiftGate = ((i + 1) < uiConfig.gates) ? (i + 1) : 0;
                  timeToChar(timeChar, idData[j].lastCheckPoint[shiftGate]);
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
    char confJSON[JSON_BUFFER_CONF];

    if (Serial.available())
    {
    // char test[128] = "test";
    
        byte inByte = Serial.read();

        switch (inByte) {
        case 'S': //tart init timer
            raceState = START;
            Serial.println("Start");
            break;

        case 'T': //sTop End connection
            raceState = STOP;
            Serial.println("Stop");
            break;

        case 'R': //eset Value
            raceState = RESET;
            Serial.println("Reset");
            break;
       
        case 'F': //ill Test Message
            //char JSONconf[JSON_BUFFER_CONF];
            JSONToConf(JSONconfDebug);
            #if defined(HAVE_WIFI)

            confToJSON(nullptr);
            // ws.textAll(confJSON);
            #endif
            Serial.println("Fill");
            break;

        case 'E': //mpty Test Message
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
void timeToChar(char *buf, uint32_t tmpMillis) { // always int len = 10,
    unsigned long nowMillis = tmpMillis;
    uint32_t tmp_seconds = nowMillis / 1000;
    uint32_t seconds = tmp_seconds % 60;
    uint16_t ms = nowMillis % 1000;
    uint32_t minutes = tmp_seconds / 60;

    snprintf(buf, 10, "%02d:%02d.%03d", minutes, seconds, ms);
}


// ----------------------------------------------------------------------------
// set Random Color to fill player data. IE: no used for the moment.
// ----------------------------------------------------------------------------
void getRandomColor(char *output, uint8_t len) {
    const char hexValue[17] = "0123456789ABCDEF";

    char color[8] = {};

    color[0] = '#';

    for (uint8_t i = 1; i < 7; i++)
    {
        uint8_t randomArray = random(16);
        color[i] = hexValue[randomArray];
    }
    strncpy(output, color, len);
}


// ----------------------------------------------------------------------------
// set Random Name to fill player data. IE: no used for the moment.
// ----------------------------------------------------------------------------
void getRandomName(char *output, uint8_t len) {

}


// ----------------------------------------------------------------------------
// DEBUG use only! set Random ID to fill player data. IE: no used for the moment.
// ----------------------------------------------------------------------------
void getRandomIDDebug(char *output, uint8_t len) {

}


// ----------------------------------------------------------------------------
// CRC8 algo, almost readable, same as the emitter one.
// ----------------------------------------------------------------------------
uint8_t CRC8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    while (len--)
    {
        uint8_t extract = *data++;
        for (uint8_t i = 8; i; i--)
        {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum)
            {
                crc ^= 0x8C;
            }
            extract >>= 1;
        }
    }
    return crc;
}


// ----------------------------------------------------------------------------
//  I2C command to send. todo: make a more enjoyable function integrating voice/sound/light
//  byte startByte[] = { 0x03, 0xB9, 0x01};
//  byte resetByte[] = { 0x03, 0xB0, 0x02};
//  byte sendFakeID[] = {0x13, ...}; 
// ----------------------------------------------------------------------------
void setCommand(const uint8_t addressSendGate, const uint8_t *command, const uint8_t commandLength){
    Wire.beginTransmission(addressSendGate);
    Wire.write(command, commandLength);
    Wire.endTransmission();
}


// ----------------------------------------------------------------------------
//  Get info from gate(s) (0x83)
// ----------------------------------------------------------------------------
void processGateData(const uint8_t *dataArray) {

    uint8_t gateTmp = dataArray[1];

    for (uint8_t i = 0 ; i < NUMBER_GATES_MAX; i++)
    {
        if (gateTmp == gateData[i].address || gateData[i].address == 0)
        {
            if (gateData[i].address == 0)
            {
                gateData[i].address = dataArray[1];
            }
            // gateData[i].checksum = dataArray[2];

            uint32_t stampTmp = ( ((uint32_t)dataArray[6] << 24)
                                + ((uint32_t)dataArray[5] << 16)
                                + ((uint32_t)dataArray[4] <<  8)
                                + ((uint32_t)dataArray[3]) );
         
            gateData[i].currentTime = stampTmp;
            break;
        }
    }
}


// ----------------------------------------------------------------------------
//  get ID data from gate(s) (0x84)  and fill idData
// ----------------------------------------------------------------------------
void processIDData(const uint8_t *dataArray) { // , const uint8_t gateNumber) {

    uint8_t gateTmp = dataArray[1];
    uint8_t crcTmp = dataArray[2];

    uint32_t idTmp = ( ((uint32_t)dataArray[6] << 24)
                     + ((uint32_t)dataArray[5] << 16)
                     + ((uint32_t)dataArray[4] <<  8)
                     + ((uint32_t)dataArray[3]) );

    uint32_t msTmp = ( ((uint32_t)dataArray[10] << 24)
                     + ((uint32_t)dataArray[9] << 16)
                     + ((uint32_t)dataArray[8] <<  8)
                     + ((uint32_t)dataArray[7]) );

    uint8_t hit = dataArray[11]; // don't know where to display...
    uint8_t strength = dataArray[12]; // don't know where to display...

    // todo: msTmp & offset & ...
    bufferingID(idTmp, gateTmp, msTmp, hit, strength);
}


// ----------------------------------------------------------------------------
//  get debug from gate(s) and fill gateData
//  fusion with processGateData ?
// ----------------------------------------------------------------------------
void processReceiverData(const uint8_t *dataArray) { //, const uint8_t gateNumber) {

    uint8_t gateTmp = dataArray[1];

    for (uint8_t i = 0 ; i < NUMBER_GATES_MAX; i++)
    {
        if (gateTmp == gateData[i].address || gateData[i].address == 0)
        {
            if (gateData[i].address == 0)
            {
                gateData[i].address = dataArray[1];
            }
            // gateData[i].checksum = dataArray[2];

            gateData[i].receiverState = dataArray[3];
            gateData[i].deltaOffsetGate = dataArray[4];
            gateData[i].bytesInBuffer = dataArray[5];
            gateData[i].loopTime = dataArray[6];         
            break;
        }
    }
}

// ----------------------------------------------------------------------------
// Future template to test speed improvement the len is always 10... "00:00.000"
// ----------------------------------------------------------------------------
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