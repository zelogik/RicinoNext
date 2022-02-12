//API i2c result send:
// 0x83, pong with time, and receiver status.
// 0x84, pong with ID data

// receive Byte possible
// 0x8A, start byte
// 0x8F, stop Byte
// 0x80, ping Byte

// i2c send loop // RESET only if sure to have send ?
// sent struct AND set to 0 all parameters after.

#include <Wire.h>

#include <SoftwareSerial.h>

#define RX PIN_PB1 //9
#define TX PIN_PB0 //8

#define LED_PIN PIN_PD2

// Only used to debug?
SoftwareSerial SoftSerialDebug(RX,TX); // got 64Bytes of buffer that we will use mainly, buffer overflow... normally not, need more than 5cars in more than 300ms...

#define PACKET_SIZE 13
//#define ID_INFO_LENGTH 13
#define I2C_ADDRESS 21 // need to be different for each board...

volatile bool pingPongTrigger = false;




volatile uint32_t offsetTime; // it's time uC - start time

//remove global here!

enum RECEIVER_state { 
    DISCONNECTED, // check i2c or power on master, trying to auto-connect
    CONNECTED, // slowing status  blink led, ping
    START, // steady status led, time running, ping, poll IRDA uC
    RACE, // waiting message Request essentially
    STOP // Back to CONNECTED /reset everything ?
};

volatile RECEIVER_state receiverState = DISCONNECTED;


struct ID_Buffer {
    bool isPending = false;
    uint8_t info[PACKET_SIZE] = {};
};

ID_Buffer idBuffer;


struct GATE_Info {
    uint32_t offsetTime; // change to int32_t ?
    uint8_t i2cAddress = I2C_ADDRESS;
    uint32_t deltaOffsetGate = 0; // it's offsetTime vs RobiBridge, change to int32_t ?
    uint8_t loopTime;
    uint8_t bufferUsed;
};

GATE_Info gateInfo;


class Led // Main class for led blinking rate.
{
private:
    uint8_t _ledPin;
    uint32_t OnTime = 100;     // milliseconds of on-time
    uint32_t OffTime = 100;    // milliseconds of off-time
    bool ledState = HIGH;                 // ledState used to set the LED
    uint32_t previousMillis;   // will store last time LED was updated
//    RECEIVER_state *raceState;

    void setOutput(bool state_, uint32_t currentMillis_) {
        ledState = state_;
        previousMillis = currentMillis_;
        digitalWrite(_ledPin, state_);
    }

    void set(uint32_t on, uint32_t off) {
        OnTime = on;
        OffTime = off;
    }

    void setBlinkRate() {
        switch (receiverState)
        {
            case DISCONNECTED:
                set(200,200);
                break;

            case CONNECTED:
                set(1000,1000);
                break;

            case START:
            case STOP:
                set(100,1000);
                break;
            
            default:
                break;
        }
    }

public:
    Led(uint8_t _ledPin) //, RECEIVER_state *_raceState)
    {
        this->_ledPin = _ledPin;
        pinMode(_ledPin, OUTPUT);
        previousMillis = 0;
//        raceState = &_raceState;
    }

    void loop(){
        uint32_t currentMillis = millis();

        setBlinkRate();
        
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

Led ledStatus = Led(LED_PIN); //, receiverState);


void setup(void) {
  Serial.begin(38400); // connected to IRDA -> UART
  SoftSerialDebug.begin(9600); // connected for debugging

  Wire.begin(I2C_ADDRESS);    // join i2c bus with address
  Wire.onRequest(requestEvent); // register event  
  Wire.onReceive(receiveEvent); // register event

}

void loop(void) {
    uint32_t startLoopTime = micros();

    // size_t *loopTime = &gateInfo.loopTime;
    ledStatus.loop();

    // purgeSerialLoop();
    raceLoop();
    processingGate();
    irdaBufferRefresh();

    loopTimeRefresh(startLoopTime);
}

void raceLoop() {
    const uint32_t pingPongDelay = 2000; // two second before deconnecting ?
    static uint32_t pingPongTimer = millis();
//    SoftSerialDebug.println(startLoopTime);
    
    if (pingPongTrigger)
    {
        pingPongTimer = millis();
        pingPongTrigger = false;
        requestToController();
    }

    if (millis() - pingPongTimer > pingPongDelay)
    {
        receiverState = DISCONNECTED;
    }

    // todo: auto if receiverState == RACE and error...

    switch (receiverState)
    {
        case START:
            resetGate(true);
            offsetTime = millis();
            receiverState = RACE;
            break;

        case STOP:
            resetGate(false);
            receiverState = CONNECTED;
            break;

        default:
            break;
    }
}


// ----------------------------------------------------------------------------
//  We don't need robiGate "seconds ping" when we don't run
// todo: find the STOP BYTE
// todo: remove that ugly black Box on this Opensource project :-D
// ----------------------------------------------------------------------------
void purgeSerialLoop() {
    if (! receiverState >= START)
    {
        uint8_t havePurged = 0;
        while (Serial.available()){
            havePurged = Serial.available(); //SoftWareSerial don't show remaining Bytes, only true or false...
            Serial.read();   
            //use of .flush();
        }
    }
}


// ----------------------------------------------------------------------------
//  Send I2C, because pingPongTrigger (controller asked)
// ----------------------------------------------------------------------------
void requestToController() {
    uint8_t I2C_Packet[13] = {};
    uint8_t I2C_length = 0;

    if (idBuffer.info[2] = 0x84)
    {
        // current idInfo.info Thanks FlipSideRacing !
        // Byte 1 is the length of the packet including this byte 0D for car detected packets
        // Byte 2 is the checksum
        // Byte 3 is the type of packet, 84 if this has car information
        // Byte 4-5 represent the UID of the car in reverse byte order
        // Byte 6-7 unknown, 00 in examples given
        // Byte 8-11 are the seconds in thousandths of a second in reverse byte order
        // Byte 12 is the number of hits the lap counter detected
        // Byte 13 is the signal strength

        uint8_t I2C_Packet[13] = {};

        I2C_Packet[0] = 0x84; // Code saying it's an ID info
        I2C_Packet[1] = gateInfo.i2cAddress;

        I2C_Packet[3] = idBuffer.info[3]; // id
        I2C_Packet[4] = idBuffer.info[4]; //
        I2C_Packet[5] = idBuffer.info[5]; //
        I2C_Packet[6] = idBuffer.info[6]; // should be 0x00 for a 24bits Ir code

        I2C_Packet[7] = idBuffer.info[7]; // Reverse Order Decode needed for time in millisSeconds
        I2C_Packet[8] = idBuffer.info[8]; //
        I2C_Packet[9] = idBuffer.info[9]; //
        I2C_Packet[10] = idBuffer.info[10]; //

        I2C_Packet[11] = idBuffer.info[11]; // number Hit
        I2C_Packet[12] = idBuffer.info[11]; // Strength


        uint8_t DataToSend[10]; // Id to Strenght for the CRC calculation
        
        for (uint8_t i = 0; i < 10; i++){
            DataToSend[i] = I2C_Packet[i + 3]; // just remove code, address, and checksum
        }        
        I2C_Packet[2] = CRC8(DataToSend, sizeof(DataToSend)); // need checksum function
        // // Clear idInfo Array.. too soon ?
        // for (uint8_t i = 0; i < idInfo[0]; i++){
        //     idInfo[i] = 0;
        // }
        idBuffer.isPending = false;
        I2C_length = 13;
    }
    else if (idBuffer.info[2] = 0x83) // Gate ping
    {

//            timeTemp = millis() - offsetTime;
        I2C_Packet[0] = 0x83;
        I2C_Packet[1] = gateInfo.i2cAddress;

        I2C_Packet[3] = receiverState; // Better to direct assign the right number ..?!?!
        I2C_Packet[4] = gateInfo.deltaOffsetGate; //!buffer overflow!!! maybe pass to 2x 8bits... because only 255ms max...

        I2C_Packet[5] = gateInfo.bufferUsed;
        I2C_Packet[6] = gateInfo.loopTime;

        uint8_t DataToSend[] = {I2C_Packet[3], I2C_Packet[4], I2C_Packet[5], I2C_Packet[6]};
        I2C_Packet[2] = CRC8(DataToSend, sizeof(DataToSend)); // need checksum function
        I2C_length = 7;
    }
    else
    {  // Unknow message...
        I2C_Packet[0] = 0x85;
        I2C_Packet[1] = gateInfo.i2cAddress;
        I2C_length = 2;
    }

    // if (I2C_length != 0) 
    // {
    Wire.write(I2C_Packet, I2C_length); //sizeof(arrayToSend) / sizeof(arrayToSend[0]));
}


// ----------------------------------------------------------------------------
//  Update loopTime, in 1/10 of ms.
// ----------------------------------------------------------------------------
void loopTimeRefresh(uint32_t startLoopTime){
    uint32_t stopLoopTime = micros();
    uint32_t calculation = stopLoopTime - startLoopTime;

    // 205/2048, around 0.1, should be faster but 0,1% error
    calculation = ((calculation << 5) * 205L) >> 11;
    if (calculation > 254){ // if we got > 254 we have loop problem
        calculation = 255;
    }
    else if (calculation < 1){ // if we got 0, we got problem too...
        calculation = 1;
    }

    if (calculation > gateInfo.loopTime)
    {
        gateInfo.loopTime = calculation;
    }
}


// ----------------------------------------------------------------------------
//  Update used buffer in Serial
// ----------------------------------------------------------------------------
void irdaBufferRefresh() {
    uint8_t bytesInBuffer = (64 - (uint8_t)Serial.available()) * 4; // default Serial buffer is 64bytes
    gateInfo.bufferUsed = bytesInBuffer;
}


// Processing messge of RobiGate only One at a time.
void processingGate(){
    static uint32_t whileTimeoutDelay = 5;

    // todo: purgeSerial is raceState < START ...
    if (!idBuffer.isPending)
    {
        uint8_t tmpBuff[PACKET_SIZE] = {};

        if (Serial.available())
        {
            tmpBuff[0] = Serial.read();
        
            for (uint8_t i = 1; i < tmpBuff[0]; i++)
            {
                uint32_t whileTimeoutTimer = millis();
                // As a complete message depend of the length of the first byte, waiting next bit is a good solution, is case of being too fast...
                while(!Serial.available())
                { // waiting loop for next data, block main loop but no much...
                    delay(1);
                    if( millis() - whileTimeoutTimer > whileTimeoutDelay){
                        // put some error text/info ?
                        break;
                    }
                }
                tmpBuff[i] = Serial.read();
            }
            // serial2DebugOutput(idInfo, sizeof(idInfo));
        }

        // Update deltaOffsetGate
        if (tmpBuff[2] == 0x83) // is time
        {
            uint32_t timeRobi = ( ((uint32_t)tmpBuff[6] << 24)
                                + ((uint32_t)tmpBuff[5] << 16)
                                + ((uint32_t)tmpBuff[4] <<  8)
                                + ((uint32_t)tmpBuff[3] ) );

            gateInfo.deltaOffsetGate = abs(timeRobi - gateInfo.offsetTime);
        }
        else if (tmpBuff[2] == 0x84) // is ID
        {
            for (uint8_t i = 0; i < PACKET_SIZE; i++) // replace by memcpy?
            {
                idBuffer.info[i] = tmpBuff[i];
            }
            idBuffer.isPending = true;
        }
    }
}


// todo: need to find the stop byte array
void resetGate(bool state){ //0 = stop, 1= reset, stop?
    byte startByte[] = { 0x03, 0xB9, 0x01};
    byte resetByte[] = { 0x03, 0xB0, 0x02};
    switch (state) {
    case false: // init timer
        Serial.write(startByte, sizeof(startByte));
        break;

    case true: // End connection
        Serial.write(resetByte, sizeof(resetByte));
        break;
    }
}


// if no Data, just send 0x82 | ReceiverAddress | pseudo checksum | State: 1= CONNECTED, 3= RACE... | last delta between RobiTime and offsetTime  0.001s/bit | Serial Buffer percent 0-255 | Loop Time in 1/10 of ms. 25ms = 250, 0.1ms or less = 1
// if messagePending, master will receive: 0x83 | ReceiverAddress | Checksum  | ID, TIME, (4bytes (ID) + 4bytes (uint32_t) + 1byte signal Strenght + 1byte Signal count ) 
// - Simple pong: 0x82 | ReceiverAddress | Checksum | State: 1= CONNECTED, 3= RACE... | last delta between RobiTime and offsetTime  0.001s/bit | Serial Buffer percent 0-255 | Loop Time in 1/10 of ms.
// - ID lap data: 0x83 | ReceiverAddress | Checksums | ID 4bytes (reversed) | TIME 4bytes (reversed) | signal Strenght | Signal Hit


// todo: set only flag and reduce interrupt function
void requestEvent() {
    // uint8_t dataLength;
    pingPongTrigger = true;
    // uint32_t timeTemp = millis() - offsetTime;
}

// receive ping, init, or stop byte.
// todo: set only flag and reduce interrupt function
void receiveEvent(int howMany)
{
    uint8_t receivedByte[howMany] = {}; // only two Bytes now... 

    uint8_t arrayIncrement = 0;
    while (Wire.available()) 
    { 
        receivedByte[arrayIncrement++] = Wire.read();
    }

    switch (receivedByte[0])
    {
    case 0x8A: // start byte
        receiverState = START;
        break;

    case 0x8F: // stop byte
        receiverState = STOP;
        break;

    default:
        break;
    }
}


// void serial2DebugOutput(uint8_t idInfoSerial[], uint8_t idInfoSize){
  
//   uint32_t timeRobi = 0;
//   uint32_t idRobi = 0;
  
//   if (idInfoSerial[2] == 0x84){ // car pass
//       timeRobi = ( ((uint32_t)idInfoSerial[10] << 24)
//                + ((uint32_t)idInfoSerial[9] << 16)
//                + ((uint32_t)idInfoSerial[8] <<  8)
//                + ((uint32_t)idInfoSerial[7] ) );
//       idRobi = ( ((uint32_t)idInfoSerial[6] << 24)
//                + ((uint32_t)idInfoSerial[5] << 16)
//                + ((uint32_t)idInfoSerial[4] <<  8)
//                + ((uint32_t)idInfoSerial[3] ) );

//       SoftSerialDebug.print("timer: ");
//       SoftSerialDebug.print(timeRobi, DEC);
//       SoftSerialDebug.print(" | ");
//       SoftSerialDebug.print("#id: ");
//       SoftSerialDebug.print(idRobi, DEC);
//       SoftSerialDebug.print(" | ");
//       SoftSerialDebug.print("hit : ");
//       SoftSerialDebug.print(idInfoSerial[11], DEC);
//       SoftSerialDebug.print(" | ");
//       SoftSerialDebug.print("strength : ");
//       SoftSerialDebug.print(idInfoSerial[12], DEC);
//       SoftSerialDebug.println();
//   }
//   if (idInfo[2] == 0x83) // timer
//   {
//       timeRobi = ( ((uint32_t)idInfoSerial[6] << 24)
//                + ((uint32_t)idInfoSerial[5] << 16)
//                + ((uint32_t)idInfoSerial[4] <<  8)
//                + ((uint32_t)idInfoSerial[3] ) );
//       SoftSerialDebug.print("timer : ");
//       SoftSerialDebug.print(timeRobi, DEC);
//       SoftSerialDebug.print(" | ");
//       SoftSerialDebug.print(Serial.available());
//       SoftSerialDebug.println();
//   }
  
// }


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
