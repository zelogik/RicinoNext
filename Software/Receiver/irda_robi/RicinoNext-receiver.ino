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

#define GATE_ID_PIN1 PIN_PD5
#define GATE_ID_PIN2 PIN_PD6
#define GATE_ID_PIN3 PIN_PD7

// Only used to debug?
SoftwareSerial SoftSerialDebug(RX,TX); // got 64Bytes of buffer that we will use mainly, buffer overflow... normally not, need more than 5cars in more than 300ms...

#define PACKET_SIZE 13
//#define ID_INFO_LENGTH 13
#define I2C_ADDRESS 21 // need to be different for each board...

volatile bool pingPongTrigger = false;

volatile uint32_t offsetTime; // it's time uC - start time

volatile uint32_t fakeIDdebug[13] = {};

//remove global here!

enum RECEIVER_state { 
    DISCONNECTED, // check i2c or power on master, trying to auto-connect
    CONNECTED, // slowing status  blink led, ping
    START, // steady status led, time running, ping, poll IRDA uC
    RACE, // waiting message Request essentially
    STOP // Back to CONNECTED /reset everything ?
};

volatile RECEIVER_state receiverState = DISCONNECTED;


struct MSG_Buffer {
    bool isPending = false;
    uint8_t array[PACKET_SIZE] = {};
    uint32_t offsetTime; // change to int32_t ?
    uint8_t i2cAddress = I2C_ADDRESS;
    uint32_t deltaOffsetGate = 0; // it's offsetTime vs RobiBridge, change to int32_t ?
    uint8_t loopTime;
    uint8_t bufferUsed;
};

MSG_Buffer msgBuffer;


class Led // Main class for led blinking rate.
{
private:
    uint8_t _ledPin;
    uint32_t OnTime;     // milliseconds of on-time
    uint32_t OffTime;    // milliseconds of off-time
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
                set(100,100);
                break;

            case CONNECTED:
                set(1000,1000);
                break;
            
            case RACE:
                set(1000,100);
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

    pinMode(GATE_ID_PIN1, INPUT);
    pinMode(GATE_ID_PIN2, INPUT);
    pinMode(GATE_ID_PIN3, INPUT);
    
    // I2C_ADDRESS = setup_gate_id();

    Wire.begin(I2C_ADDRESS);    // join i2c bus with address
    Wire.onRequest(requestEvent); // register event  
    Wire.onReceive(receiveEvent); // register event
}


void loop(void) {
    uint32_t startLoopTime = micros();

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

    const uint32_t gateAliveDelay = 5000;
    static uint32_t gateAliveTimer = millis();
//    SoftSerialDebug.println(startLoopTime);
    
    if (pingPongTrigger)
    {
        pingPongTimer = millis();
        pingPongTrigger = false;
        // requestToController();
        if (receiverState == DISCONNECTED)
        {
            receiverState = CONNECTED;
            gateCommand(true);
            gateCommand(false);
        }
    }

    if (millis() - pingPongTimer > pingPongDelay)
    {
        receiverState = DISCONNECTED;
    }

    switch (receiverState)
    {
        case CONNECTED:
            if (millis() - gateAliveTimer > gateAliveDelay)
            {
                gateAliveTimer = millis();
                gateCommand(false);
            }
            break;
        case START:
            gateCommand(true);
            offsetTime = millis();
            receiverState = RACE;
            break;

        case RACE:
            // todo: auto deconnect if receiverState == RACE and error...
            break;

        case STOP:
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

    if (calculation > msgBuffer.loopTime)
    {
        msgBuffer.loopTime = calculation;
    }
}


// ----------------------------------------------------------------------------
//  Update used buffer in Serial
// ----------------------------------------------------------------------------
void irdaBufferRefresh() {
    uint8_t bytesInBuffer = (64 - (uint8_t)Serial.available()) * 4; // default Serial buffer is 64bytes
    msgBuffer.bufferUsed = bytesInBuffer;
}


// ----------------------------------------------------------------------------
// Processing message (emptying serial buffer) of ATmega16 only one at a time.
//  CAR Detected: Thanks FlipSideRacing !
//    Byte 1 is the length of the packet including this byte 0D for car detected packets
//    Byte 2 is the checksum
//    Byte 3 is the type of packet, 84 if this has car information
//    Byte 4-5 represent the UID of the car in reverse byte order
//    Byte 6-7 unknown, 00 in examples given
//    Byte 8-11 are the seconds in thousandths of a second in reverse byte order
//    Byte 12 is the number of hits the lap counter detected
//    Byte 13 is the signal strength
//  Time stamp packet
//    Byte 1 is the length of the packet 0B in the case of just a time stamp
//    Byte 2 is a checksum
//    Byte 3 is the packet type, 83 in the case of time stamp only
//    Byte 4-7 is the seconds in reverse byte order in thousandths of a second
//    Bytes 8-11 are unknown but in example were always 14 D0 01 02
// ----------------------------------------------------------------------------
void processingGate(){
    static uint32_t whileTimeoutDelay = 5;

    // todo: purgeSerial is raceState < START ...
    if (!msgBuffer.isPending)
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
        else if (fakeIDdebug[0] == 0x13)
        {  //set the fakde ID received from controller
            for (uint8_t i = 0; i < 13; i++) // replace by memcpy?
            {
                tmpBuff[i] = fakeIDdebug[i]; // change by howMany...
            }
            tmpBuff[0] = 0x13;
            // tmpBuff[1] = 0x0D; // multi gate faker
            // tmpBuff[2] = 0x84;
            fakeIDdebug[0] = 0; //reset
            msgBuffer.isPending = true;
        }

        // Update deltaOffsetGate
        if (tmpBuff[2] == 0x83) // is time
        {
            for (uint8_t i = 0; i < tmpBuff[0]; i++) // replace by memcpy?
            {
                msgBuffer.array[i] = tmpBuff[i];
            }
            uint32_t timeRobi = ( ((uint32_t)tmpBuff[6] << 24)
                                + ((uint32_t)tmpBuff[5] << 16)
                                + ((uint32_t)tmpBuff[4] <<  8)
                                + ((uint32_t)tmpBuff[3] ));

            msgBuffer.deltaOffsetGate = abs(timeRobi - msgBuffer.offsetTime);
            msgBuffer.isPending = true;
        }
        else if (tmpBuff[2] == 0x84) // is ID / fakeID
        {
            for (uint8_t i = 0; i < tmpBuff[0]; i++) // replace by memcpy?
            {
                msgBuffer.array[i] = tmpBuff[i];
            }
            msgBuffer.isPending = true;
        }
        else
        {
            msgBuffer.array[2] == 0x82;
        }
    }
}


// ----------------------------------------------------------------------------
// Gate command send... to AtMega16
// ----------------------------------------------------------------------------
void gateCommand(bool state){ //0 = stop, 1= reset, stop?
    byte startByte[] = { 0x03, 0xB9, 0x01}; // at start
    byte resetByte[] = { 0x03, 0xB0, 0x02}; // every 5sec
    
    if (state)
    {
        Serial.write(startByte, sizeof(startByte));
    }
    else
    {
        Serial.write(resetByte, sizeof(resetByte));
    }

}


// ----------------------------------------------------------------------------
//  Send I2C, because pingPongTrigger (controller asked)
//  todo/bug: set a flag and send outside interrupt with a function doesn't work...
// - Pong: 0x82 | ReceiverAddress | Checksum | State: 1= CONNECTED, 3= RACE... | last delta between RobiTime and offsetTime  0.001s/bit | Serial Buffer percent 0-255 | Loop Time in 1/10 of ms.
// - ID:   0x84 | ReceiverAddress | Checksum | ID 4bytes (reversed) | TIME 4bytes (reversed) | signal Strenght | Signal Hit
// - Gate: 0x83 | ReceiverAddress | Checksum | TIME 4bytes (reversed)
// ----------------------------------------------------------------------------
void requestEvent() {
    // uint8_t dataLength;
    pingPongTrigger = true;

    uint8_t I2C_Packet[13] = {0};
    // uint8_t I2C_length = 0;

    if (msgBuffer.isPending)
    {
        if (msgBuffer.array[2] == 0x84)
        {   // Code saying it's an ID info
            I2C_Packet[0] = 0x84;

            if (msgBuffer.array[0] == 0)
            {
                I2C_Packet[1] = msgBuffer.i2cAddress;
            }
            else
            {
                I2C_Packet[1] = msgBuffer.array[1]; // or fake gate...
            }

            I2C_Packet[3] = msgBuffer.array[3]; // id
            I2C_Packet[4] = msgBuffer.array[4]; //
            I2C_Packet[5] = msgBuffer.array[5]; //
            I2C_Packet[6] = msgBuffer.array[6]; // should be 0x00 for a 24bits Ir code

            I2C_Packet[7] = msgBuffer.array[7]; // Reverse Order Decode needed for time in millisSeconds
            I2C_Packet[8] = msgBuffer.array[8]; //
            I2C_Packet[9] = msgBuffer.array[9]; //
            I2C_Packet[10] = msgBuffer.array[10]; //

            I2C_Packet[11] = msgBuffer.array[11]; // number Hit
            I2C_Packet[12] = msgBuffer.array[12]; // Strength

            uint8_t DataToSend[10]; // Id to Strength for the CRC calculation
            
            for (uint8_t i = 0; i < 10; i++){
                DataToSend[i] = I2C_Packet[i + 3]; // just remove code, address, and checksum
            }        
            I2C_Packet[2] = CRC8(DataToSend, sizeof(DataToSend));
        }
        else if (msgBuffer.array[2] == 0x83) // Gate ping
        {  // Code saying it's a timeStamp
    //            timeTemp = millis() - offsetTime;
            I2C_Packet[0] = 0x83;
            I2C_Packet[1] = msgBuffer.i2cAddress;

            I2C_Packet[3] = msgBuffer.array[3]; // id
            I2C_Packet[4] = msgBuffer.array[4]; //
            I2C_Packet[5] = msgBuffer.array[5]; //
            I2C_Packet[6] = msgBuffer.array[6]; // should be 0x00 for a 24bits Ir code

            uint8_t DataToSend[] = {I2C_Packet[3], I2C_Packet[4], I2C_Packet[5], I2C_Packet[6]};
            I2C_Packet[2] = CRC8(DataToSend, sizeof(DataToSend)); // need checksum function
        }
    
        msgBuffer.isPending = false;
    }
    else
    {  // Unknow message... or debug log/stat ?
        I2C_Packet[0] = 0x82;
        I2C_Packet[1] = msgBuffer.i2cAddress;

        I2C_Packet[3] = receiverState; // Better to direct assign the right number ..?!?!
        I2C_Packet[4] = msgBuffer.deltaOffsetGate; //!buffer overflow!!! maybe pass to 2x 8bits... because only 255ms max...

        I2C_Packet[5] = msgBuffer.bufferUsed;
        I2C_Packet[6] = msgBuffer.loopTime;
    }

    Wire.write(I2C_Packet, 13); //sizeof(arrayToSend) / sizeof(arrayToSend[0]));
}


// receive ping, init, or stop byte.
// todo: set only flag and reduce interrupt function
void receiveEvent(int howMany)
{
    uint8_t receivedByte[howMany] = {}; // only XXX Bytes now... 

    uint8_t arrayIncrement = 0;
    while (Wire.available()) 
    { 
        receivedByte[arrayIncrement] = Wire.read();
        arrayIncrement++;
    }

    switch (receivedByte[0])
    {
    case 0x8A: // start byte
        receiverState = START;
        break;

    case 0x8F: // stop byte
        receiverState = STOP;
        break;
        
    case 0x13: // debug mode
        // simulation IR code ?
        // it's an fake ID ! yes !!
        for (uint8_t i = 0; i < 13; i++) // replace by memcpy?
        {
            fakeIDdebug[i] = receivedByte[i]; // change by howMany...
        }
        break;

    default:
        break;
    }
}


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


// ADD an automatic address set with shunt/pin
uint8_t setup_gate_id(){
    uint8_t id_gate = 0;
    const uint8_t i2c_address[] = {0,21,22,23,24,25,26,27,28};

    id_gate = digitalRead(GATE_ID_PIN1) ? 1 : 0;
    id_gate = digitalRead(GATE_ID_PIN2) ? id_gate + 2 : id_gate;
    id_gate = digitalRead(GATE_ID_PIN3) ? id_gate + 4: id_gate;

    //  if (id_gate == 0) -> no solder and so fixed number!
    return i2c_address[id_gate];
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
