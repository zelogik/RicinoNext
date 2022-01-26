#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsync_WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include <FS.h>
#include <Wire.h>          
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include "Button2.h"

#define NUMBER_PLAYERS 32 //ESP32 have PLENTY of SRAM when code is optimized! humhum...
#define PACKET_SIZE 13     // I2C packet size, slave will send 14 bytes to master
#define ARRAY_LENGTH(x)  ( sizeof( x ) / sizeof( *x ) )

#define IR_PIN_TEMP 27
#define RXD2 25
#define TXD2 26

#define BUTTON_1  35
#define BUTTON_2  0

Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

TFT_eSPI tft = TFT_eSPI(135, 240);  // Invoke library, pins defined in User_Setup.h

/* AsyncWebServer Stuff */
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

const uint8_t addressAllGates[] = {21}; //, 22, 23}; // Order: 1 Gate-> ... -> Final Gate!
const uint8_t NUMBER_GATES = ARRAY_LENGTH(addressAllGates);

typedef enum {
  WAIT,          // Set parameters(Counter BIP etc...), before start counter
  BEEP,          // Only run beep before gotoSTART
  START,        // Racer can register, timeout after 20s, to to RACE
  RACE,         // update dataRace().
  FINISH,         // 1st player have win, terminate after 20s OR all players have finished.
  STOP         // finished auto/manual, reset every backend parameters
} Race_State;

// Update Ready
typedef struct {
    bool isIdUpdate = false;
    // uint8_t updateIdBuffer[8] = {};
    uint8_t idOrderUpdate[NUMBER_PLAYERS] = {};
    const uint8_t startCmd[2] = {0x8A, 0x00};
    const uint8_t stopCmd[2] = {0x8F, 0x00};
    uint32_t startOffset = 0;
    uint16_t finishSeconds = 20; // time in Sec not ms
    uint16_t raceLap = 10;
} APP_Data;

// Buffer for player/id history
typedef struct {
    uint32_t offsetLap = 0;
    uint8_t lastGateTriggered;
    uint32_t prevTimeTriggered;
    bool missedGate = false;

    uint32_t id; // ir Code
    uint8_t countGateTriggered[NUMBER_GATES];
    
    uint32_t lastFullLap;
    uint32_t bestFullLap;
    uint32_t meanFullLap;
    uint32_t totalLap;

    // Need a struct with variable length contening each time....
    // Could reduce to 16bits, and get a 0.1s precision to get around overflow. 
    uint32_t fullTimeCheckPoint[NUMBER_GATES];
    uint32_t lastCheckPoint[NUMBER_GATES];
    uint32_t bestCheckPoint[NUMBER_GATES];
    uint32_t totalMeanCheckPoint[NUMBER_GATES];
    uint32_t meanCheckPoint[NUMBER_GATES];
    int32_t deltaPrevCheckPoint[NUMBER_GATES];
} ID_Data; // 8 players example (60Bytes/id)

// Struct of for Gates information
typedef struct {
    uint8_t address;
    uint8_t receiverState; //0-255 to 0-100%
    uint8_t deltaOffsetGate; //0-255 to 0-100%
    uint8_t bytesInBuffer; //typo wait
    uint8_t loopTime; //wait
} Gate_Data;


// Buffer_Data bufferData;
ID_Data idData[NUMBER_PLAYERS];
Gate_Data gateData[NUMBER_GATES];
APP_Data appData;

Race_State raceState = WAIT;

void setup () 
{
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  Wire.begin();

  pinMode(IR_PIN_TEMP, OUTPUT);

  button_init();

  tft.init();
  tft.fillScreen(TFT_BLACK);
  tft.setRotation(1);
}


// Global working algo:
// Send events:
// The I2C master send command (START, STOP) to Gate receiver
// I2C master could send command like (speakTIME, RELAY_ON/OFF, LIGHT_PWM), to others receivers (to discharge the display/master cpu)

// Request Events for GATE.
// Send "ping" request packet every X time to view status.
// receiver MUST respond as fast as possible (interrupt based)
// With two (for the moment) possibility of receiver response.
// - Simple pong: 0x82 | ReceiverAddress | Checksum | State: 1= CONNECTED, 3= RACE... | last delta between RobiTime and offsetTime  0.001s/bit | Serial Buffer percent 0-255 | Loop Time in 1/10 of ms.
// - ID lap data: 0x83 | ReceiverAddress | Checksums | ID 4bytes (reversed) | TIME 4bytes (reversed) | signal Strenght | Signal Hit

void loop()
{
    static uint8_t serialDisplayCounter = 0;
    APP_Data *app_ptr;
    app_ptr = &appData;

    gatePing();
    pulseIRLoop(false);
    btn1.loop();
    btn2.loop();

    switch (raceState)
    {
    case WAIT:
        break;

    case BEEP:
        // beep();
        // light();
        raceState = START;
        break;

    case START:
        for (uint8_t i = 0; i < NUMBER_GATES; i++)
        {
            setCommand(addressAllGates[i], app_ptr->startCmd, sizeof(&app_ptr->startCmd));
        }
        app_ptr->startOffset = millis();  //(*ptr).offsetLap;
        raceState = RACE;
        break;

    case RACE:
        // test if first player > max LAP
        //  raceState = FINISH;
        break;

    case FINISH:
        // if ALL player > MAX LAP or timeOUT
        //  raceState = STOP;
        break;

    case STOP:
        break;
    
    default:
        break;
    }

    serialInput();
    processOLED();
    processHTTP();
    serialDebug();
}


void processOLED(){

}


void processHTTP(){

}


void setCommand(uint8_t addressSendGate, const uint8_t *command, uint8_t commandLength){
    Wire.beginTransmission(addressSendGate);
    Wire.write(command, commandLength);
    Wire.endTransmission();
}


void gatePing(){
    static uint32_t gateRequestTimer = millis();
    const uint32_t gateRequestDelay = 100;
    
    if (millis() - gateRequestTimer >= gateRequestDelay){

        gateRequestTimer = millis();

        for (uint8_t i = 0 ; i < NUMBER_GATES; i++)
        {
           getDataFromGate(addressAllGates[i]);
        }
    }
}


// I2C Request data from slave, need to be proceessed after that.
bool getDataFromGate(uint8_t addressRequestGate){ //, bufferData* Info)
    bool state;
    uint8_t I2C_Packet[PACKET_SIZE];
    // uint32_t stupidI2CBugByte = (uint32_t)addressRequestGate;
    
    Wire.requestFrom((int)addressRequestGate, PACKET_SIZE);

    // Find a way to stop the i2c the fastest way possible if receiver send less Bytes.
    uint8_t countPosArray = 0;
    while(Wire.available())
    {
        I2C_Packet[countPosArray++] = Wire.read();
    }

    // If we got an I2C packet, we can extract the values
    switch (I2C_Packet[0])
    {
    case 0x82: //get pong data
        processIDData(I2C_Packet);
        break;

    case 0x83: // get id data
        processGateData(I2C_Packet);
        break;

    default:
        break;
    }

    return state;
}


void processIDData(uint8_t *dataArray){

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

    for (uint8_t playerPos = 0; playerPos < NUMBER_PLAYERS; playerPos++){
        if (idTmp == idData[playerPos].id || idData[playerPos].id == 0) // Take the first NULL id
        {
            if (idData[playerPos].id == 0)
            {
                idData[playerPos].id = idTmp;
            }

            uint8_t gatePos = 0;

            for (gatePos; gatePos < NUMBER_GATES; gatePos++)
            {
                if (addressAllGates[gatePos] == dataArray[0])
                {
                    break;
                }
            }

            // Next is some BrainFuck calculation ...
            if (isRightGate(playerPos, gatePos, msTmp)) // Check valid Gate
            {
                idData[playerPos].fullTimeCheckPoint[gatePos] = msTmp; // - offsetLap;
                idData[playerPos].countGateTriggered[gatePos]++;

                if (gatePos == 0) // Update Only for a Full Lap
                {
                    // idData[playerPos].countLap[0]++; // = idData[playerPos].numberLap + 1;
                    // idData[playerPos].fullTimeCheckPoint[0] = msTmp - offsetLap;
                    idData[playerPos].meanFullLap = meanCalculation(&idData[playerPos].fullTimeCheckPoint[gatePos], &idData[playerPos].countGateTriggered[gatePos]);
                    idData[playerPos].lastFullLap = idData[playerPos].fullTimeCheckPoint[0] - msTmp;
                    idData[playerPos].bestFullLap = bestCalculation(&idData[playerPos].bestFullLap, &idData[playerPos].lastFullLap);

                }

                uint32_t prevTimeTriggered = idData[playerPos].prevTimeTriggered;
                uint32_t prevCheckPoint = idData[playerPos].lastCheckPoint[gatePos];

                idData[playerPos].prevTimeTriggered = idData[playerPos].fullTimeCheckPoint[gatePos];
                idData[playerPos].lastCheckPoint[gatePos] = idData[playerPos].prevTimeTriggered - prevTimeTriggered;
                idData[playerPos].totalMeanCheckPoint[gatePos] = idData[playerPos].totalMeanCheckPoint[gatePos] + idData[playerPos].lastCheckPoint[gatePos];
                idData[playerPos].meanCheckPoint[gatePos] = meanCalculation(&idData[playerPos].totalMeanCheckPoint[gatePos], &idData[playerPos].countGateTriggered[gatePos]);
                idData[playerPos].bestCheckPoint[gatePos] = bestCalculation(&idData[playerPos].bestCheckPoint[gatePos], &idData[playerPos].lastCheckPoint[gatePos]);
                idData[playerPos].deltaPrevCheckPoint[gatePos] = idData[playerPos].lastCheckPoint[gatePos] - prevCheckPoint;
            }
        }
        break;
    }
}


void processGateData(uint8_t *dataArray){
    
    for (uint8_t i = 0; i < sizeof(addressAllGates); i++)
    {
        if (dataArray[1] == gateData[i].address || gateData[i].address == 0)
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


uint32_t bestCalculation(uint32_t *best_ptr, uint32_t *last_ptr){
    uint32_t value = *best_ptr;

    if (*last_ptr < *best_ptr || *best_ptr == 0){
        value = *last_ptr;
    }

    return value;
}


uint32_t meanCalculation(uint32_t* time_ptr, uint8_t* count_ptr){
    uint32_t value = *time_ptr / *count_ptr;
    
    return value;
}


bool isRightGate(uint8_t idStructPosition, uint8_t currentGateNumber, uint32_t timeID){
    bool status = false;
    uint8_t nextGateNumber;
    uint8_t possibleMissedGateNumber;
    
    if (NUMBER_GATES == 1)
    {
        // A simple debounce code for 1 gate counter
        if (timeID - idData[idStructPosition].totalLap > 2000){
            status = true;
        }
    }
    else
    {
        if (currentGateNumber == idData[idStructPosition].lastGateTriggered)
        {
            nextGateNumber = addressAllGates[(currentGateNumber + 1) % NUMBER_GATES];
            possibleMissedGateNumber = addressAllGates[(currentGateNumber + 2) % NUMBER_GATES];
        }

        if (nextGateNumber == currentGateNumber)
        {
            idData[idStructPosition].missedGate = false;
            idData[idStructPosition].lastGateTriggered = nextGateNumber;
            status = true;
        }
        else if (!idData[idStructPosition].missedGate && possibleMissedGateNumber == currentGateNumber)
        {
            idData[idStructPosition].missedGate = true;
            idData[idStructPosition].lastGateTriggered = possibleMissedGateNumber;
            status = true;
        }
    }

    return status;
}


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


void serialInput(){
    if (Serial.available()) {
    
    byte inByte = Serial.read();

        switch (inByte) {
        case 'S': // init timer
            raceState = START;
            break;
      
        case 'R': // End connection
            raceState = STOP;
            break;
      
        case 'F':
            break;

    
        case 'T': // toggle pin 27
            digitalWrite(IR_PIN_TEMP, HIGH);
            pulseIRLoop(true);
            break;
            
        }
    }
}


void serialDebug(){
  static uint32_t previousMillis = 0;
  const uint16_t interval = 5000;
  static uint32_t counter = 0;
  
  if (Serial2.available() > 0) {     // If anything comes in Serial2
      Serial.write(Serial2.read());   // read it and send it out Serial (USB)
  }

  if (millis() - previousMillis >= interval) {
    previousMillis = millis();
    counter++;
    Serial.print("alive : ");
    Serial.println(counter);
  }
}


void button_init()
{
    btn1.setDebounceTime(50);
    btn2.setDebounceTime(50);
    
    btn1.setTapHandler([](Button2& btn) {
        static bool state = false;
        state = !state;
        tft.fillRect(0, 134, 120, 1, state ? TFT_WHITE : TFT_BLACK);
    });

    btn2.setTapHandler([](Button2& btn) {
        static bool state = false;
        state = !state;
        tft.fillRect(120, 134, 120, 1, state ? TFT_WHITE : TFT_BLACK);
    });
}


void pulseIRLoop(bool onTag){
  static uint32_t pulseIRdelay = 0;
  static bool pulseIRbool = false;

  if (onTag){
      pulseIRbool = true;
      pulseIRdelay = millis();
  }

  if ( millis() - pulseIRdelay > 500 && pulseIRbool){
      digitalWrite(IR_PIN_TEMP, LOW);
  }
}


uint8_t CRC8(uint8_t *data, uint8_t len) {
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
