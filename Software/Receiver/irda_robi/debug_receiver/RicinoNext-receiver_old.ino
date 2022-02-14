//API i2c result send:
// 0x81, not running, so ready
// 0x82, no new data to send
// 0x83, pong, with time
// 0x84, new data to send.

// receive Byte possible
// 0x80, start byte
// 0x8F, stop Byte
// 0x8A, ping Byte

// set struct buffer // BE CAREFUL WITH INTERRUPT ?
// validation loop :
// check if last_ms - first_ms > 50ms && millis - last_ms > PING_TIME * X ? (should have passed...)
// with ir code or trigger should be a different algo?!?
// more like check if counter > 3 for example.
// if yes set isReady to true

// update loop when ir code OR trigger //THAT ONE SHOULD BE FAST
// check each buffer
// if already triggered or ir Code exist
// update last_ms AND increase counter
// else set new first_ms value

// i2c send loop // RESET only if sure to have send ?
// if isReady == true
// sent struct AND set to 0 all parameters after.


#include <Wire.h>

#define LED_PIN 13 // led status, optionnal.
#define GATE_PIN 2 // pin 2 need interrupt
#define I2C_ADDRESS 21 // need to be different for each board...
#define PACKET_SIZE 14
#define BUFFER_LEN 8

volatile bool sendPong = false;
volatile bool sendData = false;
volatile uint8_t incomingTransmission = 0; // 1, rising ; 2, falling

volatile uint32_t offsetTime; //set when master send start

typedef struct Data_T {
    uint32_t id = 0;
    uint32_t first_mS = 0; //uint24...to convert
    uint32_t last_mS = 0;
    uint8_t quality = 0;
    uint8_t counter = 0;
    bool isReady = false;
}; // dataBuf[BUFFER_LEN];

//static const Data_T dataBufTmp;

static const Data_T dataBufReset;
Data_T dataBuf[BUFFER_LEN];

//RingBufCPP<struct Data, 8> buf; // increase buffer length (check memory...)
//CircularBuffer<Data_t, 10> structs;

enum RECEIVER_STATE { 
  DISCONNECTED, // check i2c or power on master, trying to auto-connect
  CONNECTED, // slowing status  blink led, don't send trigger to i2c, time zeroed, ping
  RACE // steady status led, send trigger to i2c, time running, ping
} receiverState = DISCONNECTED;


class Led // lKeep RED and GREEN led active
{
private:
    uint8_t _ledPin;
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
    Led(uint8_t _ledPin)
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

Led ledStatus = Led(LED_PIN);

void pushSignal() {
    incomingTransmission = (PIND & (1<<PD2)) ? 2 : 1;
    // (digitalRead(GATE_PIN)) ? 2 : 1;
}

bool recordId(uint32_t Id){

    uint32_t lastTime = millis() - offsetTime;
    bool updatedBuf = false;

    for (uint8_t i = 0; i < BUFFER_LEN; i++) {
        if (dataBuf[i].id == Id) {
            dataBuf[i].counter++;
            updatedBuf = true;
            if (incomingTransmission = 2){
                dataBuf[i].last_mS = lastTime;
            }
        }
    }

    if (!updatedBuf){
        for (uint8_t i = 0; i < BUFFER_LEN; i++){
            if (dataBuf[i].id == 0){
                dataBuf[i].id = Id;
                dataBuf[i].first_mS = millis();
                dataBuf[i].counter++;;
//                Serial.print(dataBuf[i].id);
//                Serial.print(" : ");
//                Serial.println(i);
                break; //finish registration
            }
        }
    }
}


void setup() {
  Serial.begin(9600);
  Wire.begin(I2C_ADDRESS);    // join i2c bus with address #8
  Wire.onRequest(requestEvent); // register event  
  Wire.onReceive(receiveEvent); // register event

  attachInterrupt(digitalPinToInterrupt(GATE_PIN), pushSignal, CHANGE); // 
}

void loop() {
    static uint32_t fastLoopTimer = millis();
    const uint32_t fastLoopDelay = 100;

    if (incomingTransmission > 0){
        //IR, RFID, other decode process function...
        incomingTransmission = 0;
        recordId(0x100001); //temp ID
    }
    else{

        ledStatus.loop();
        readyLoop();

        if (millis() - fastLoopTimer > fastLoopDelay){
            fastLoopTimer = millis();
            uint16_t onLed = 250 * (receiverState + 1);
            uint16_t offLed = 500 * (3 - receiverState);
            ledStatus.set(onLed, offLed);
//            if (receiverState == CONNECTED)
//            {
//                ledStatus.set(500,500);
//            }
//            else if (receiverState == RACE)
//            {
//                ledStatus.set(1000,1000);
//            }
//            else
//            {
//                ledStatus.set(250,2000);
//            }
        }
    }
}


void readyLoop(){
    const uint8_t debounceDelay = 10;
    const uint8_t staticDataDelay = 1000;
    const uint16_t oldDataDelay = 4000;

    uint32_t currentTime = millis() - offsetTime;
    
    for( uint8_t i = 0; i < BUFFER_LEN; i++){
        if( dataBuf[i].id != 0 && dataBuf[i].isReady != true){
            if(currentTime - dataBuf[i].last_mS > staticDataDelay){
                if(dataBuf[i].last_mS - dataBuf[i].first_mS > debounceDelay){
                    dataBuf[i].isReady = true;
//                    Serial.println(dataBuf[i].last_mS);
                }
            }
            
//            else if( dataBuf[i].isSent == true) { // bypass bufferOverflow
//                dataBuf[i] = dataBufReset;
//                Serial.println("reset");
//    
//                resetDataBuf(i);
//    
//            }
        }
    }
    
}

//void resetDataBuf(uint8_t number){
//    // two way... create a static zero dataBuf, or change each value one by one...
//    static const Data_T dataBufTmp;
//
//    dataBuf[number] = dataBufTmp;
////    dataBuf[number].id = 0;
////    dataBuf[number].first_mS = 0;
////    dataBuf[number].last_mS = 0;
////    dataBuf[number].quality = 0;
////    dataBuf[number].counter = 0;
////    dataBuf[number].isReady = false;
//}

// if no Data, just send 0x82 and pseudo checksum...
// if buffer have data, master will request that time ID, TIME, (3bytes (ID) + 4bytes (uint32_t) + 1byte Checksum? + 1byte signal Strenght ? + ) 
void requestEvent() {
    uint8_t dataLength;
    uint8_t I2C_Packet[PACKET_SIZE];            // Array to hold data sent over I2C to main Arduino
    uint32_t timeTemp = millis() - offsetTime;
    uint8_t bufNumber = BUFFER_LEN + 1; // needed for the array check number "<"

    for(uint8_t i = 0; i < BUFFER_LEN; i++){
        if (dataBuf[i].isReady == true){
            bufNumber = i;
//            Serial.println("ready");
            break; // only one by one
        }
    }


    if (sendPong)
    {
        sendPong = false;
        if (receiverState == CONNECTED)
        {
            timeTemp = 0;
            I2C_Packet[0] = 0x81;
        }
        else if (receiverState == RACE)
        {
            timeTemp = millis() - offsetTime;
            I2C_Packet[0] = (bufNumber > BUFFER_LEN) ? 0x82 : 0x83;
            I2C_Packet[2] = (timeTemp >> 24) & 0xFF;
            I2C_Packet[3] = (timeTemp >> 16) & 0xFF;
            I2C_Packet[4] = (timeTemp >> 8) & 0xFF;
            I2C_Packet[5] = timeTemp & 0xFF;
            
            uint8_t DataToSend[] = {I2C_Packet[2], I2C_Packet[3], I2C_Packet[4], I2C_Packet[5]};

            I2C_Packet[1] = CRC8(DataToSend, sizeof(DataToSend)); // need checksum function
        }

        Wire.write(I2C_Packet, 6); //sizeof(arrayToSend) / sizeof(arrayToSend[0]));
    }


    else if (sendData)
    {
        sendData = false;
        Serial.print("dataSend:   ");
        Serial.println(dataBuf[bufNumber].counter);
        I2C_Packet[0] = 0x86;
        I2C_Packet[1] = I2C_ADDRESS; // need checksum function

        I2C_Packet[2] = (uint32_t)(0x00 & 0xFF); //32bits proof compatible Receiver...
        I2C_Packet[3] = (uint32_t)((dataBuf[bufNumber].id >> 16) & 0xFF);
        I2C_Packet[4] = (uint32_t)((dataBuf[bufNumber].id >> 8) & 0xFF);
        I2C_Packet[5] = (uint32_t)(dataBuf[bufNumber].id & 0xFF);

        I2C_Packet[7] = (uint32_t)((dataBuf[bufNumber].first_mS >> 24) & 0xFF);
        I2C_Packet[8] = (uint32_t)((dataBuf[bufNumber].first_mS >> 16) & 0xFF);
        I2C_Packet[9] = (uint32_t)((dataBuf[bufNumber].first_mS >> 8) & 0xFF);
        I2C_Packet[10] = (uint32_t)(dataBuf[bufNumber].first_mS & 0xFF);

        uint8_t DataToSend[] = {I2C_Packet[2], I2C_Packet[3], I2C_Packet[4], I2C_Packet[5], I2C_Packet[7], I2C_Packet[8], I2C_Packet[9], I2C_Packet[10]};
        I2C_Packet[6] = CRC8(DataToSend, sizeof(DataToSend)); // need checksum function
        
        I2C_Packet[11] = dataBuf[bufNumber].counter;
        
        Wire.write(I2C_Packet, PACKET_SIZE); //sizeof(arrayToSend) / sizeof(arrayToSend[0]));

        dataBuf[bufNumber] = dataBufReset;
//        resetDataBuf(bufNumber);
//        dataBuf[bufNumber].isReady = false;
    }
}


// receive ping, init, or stop byte.
void receiveEvent(int howMany)
{
    uint8_t receivedByte;
    uint32_t timeTemp;
    uint8_t arrayToSend[6];
    
    while(Wire.available()) 
    { 
        receivedByte = Wire.read(); // receive byte as a character
    }

    switch (receivedByte)
    {
    case 0x8A: // start byte
        receiverState = RACE;
        offsetTime = millis();
        break;
    
    case 0x8F: // stop byte
        receiverState = CONNECTED;
        //purge buf, nicer way?
//        struct Data e;
//        while(!buf.pull(&e)){
////            bool plop;
//        }
        break;

     case 0x80: // ping/pong byte
        sendPong = true;
        break;

     case 0x85:
        sendData = true;
        break;

    default:
        break;
    }
}

uint8_t CRC8(uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    uint8_t extract = *data++;
    for (uint8_t i = 8; i; i--) {
      uint8_t sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) {
        crc ^= 0x8C;
      }
      extract >>= 1;
    }
  }
  return crc;
}