// master reader
#include <Wire.h>

void setup() {
  Wire.begin();        // join i2c bus (address optional for master)
  Serial.begin(9600);  // start serial for output
}

void loop() {
  Wire.requestFrom(8, 6);    // request 6 bytes from slave device #8

  while (Wire.available()) { // slave may send less than requested
    char c = Wire.read(); // receive a byte as character
    Serial.print(c);         // print the character
  }

  delay(500);
}

// master writer
void setup()
{
  Wire.begin(); // join i2c bus (address optional for master)
}

byte x = 0;

void loop()
{
  Wire.beginTransmission(4); // transmit to device #4
  Wire.write("x is ");        // sends five bytes
  Wire.write(x);              // sends one byte  
  Wire.endTransmission();    // stop transmitting

  x++;
  delay(500);
}


// slave sender
#include <Wire.h>

void setup() {
  Wire.begin(8);                // join i2c bus with address #8
  Wire.onRequest(requestEvent); // register event
}

void loop() {
  delay(100);
}

// function that executes whenever data is requested by master
// this function is registered as an event, see setup()
void requestEvent() {
  Wire.write("hello "); // respond with message of 6 bytes
  // as expected by master
}


// slave receiver
#include <Wire.h>

void setup()
{
  Wire.begin(4);                // join i2c bus with address #4
  Wire.onReceive(receiveEvent); // register event
  Serial.begin(9600);           // start serial for output
}

void loop()
{
  delay(100);
}

// function that executes whenever data is received from master
// this function is registered as an event, see setup()
void receiveEvent(int howMany)
{
  while(1 < Wire.available()) // loop through all but the last
  {
    char c = Wire.read(); // receive byte as a character
    Serial.print(c);         // print the character
  }
  int x = Wire.read();    // receive byte as an integer
  Serial.println(x);         // print the integer
}






int32_t bigNum;
 
byte a,b,c,d;
 
Wire.requestFrom(54,4);
 
a = Wire.read();
b = Wire.read();
c = Wire.read();
d = Wire.read();
 
bigNum = a;
bigNum = (bigNum << 8) | b;
bigNum = (bigNum << 8) | c;
bigNum = (bigNum << 8) | d;







//===========================================//
// slave sender
#include <Wire.h>
#include <RingBufCPP.h>

#define LED_PIN 4
#define GATE_PIN 0 //pin 2 need interrupt

volatile bool pong = false;
volatile bool running = false;
volatile uint32_t offsetTime; //set when master send magic number! 0x20
volatile bool incomingTransmission = false;

struct Data {
    uint32_t time;
    uint32_t id; //uint24...to convert
    uint8_t signal;
    // bool flag;
};

RingBufCPP<struct Data, 8> buf;

// CircularBuffer<data::record, 8> structs;

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
    //test if running = true;
    incomingTransmission = true;
}

bool decodeGate(){
    ledStatus.Loop();

    struct Event e;
    
    e.time = millis() - offsetTime;
    e.id = 0x123456;
    s.ignal = 0x50;

    buf.add(e);
    // structs.unshift(data::record{passTime, passId, passSignal});
    // data::print(structs.shift());
}


void setup() {
  Wire.begin(8);                // join i2c bus with address #8
  Wire.onRequest(requestEvent); // register event  
  Wire.onReceive(receiveEvent); // register event

  attachInterrupt(digitalPinToInterrupt(GATE_PIN), pushSignal, RISING); // 
}

void loop() {
    static uint32_t fastLoopTimer = millis();
    const uint32_t fastLoopDelay = 100;

    if (incomingTransmission){
        //IR, RFID, other decode process function...
        decodeGate();
        incomingTransmission = false;
    }
    else{

        ledStatus.Loop();

        if (millis() - fastLoopTimer > fastLoopDelay){
            fastLoopDelay = millis();
            if (running)
            {
                ledStatus.set(500,500)
            }
            else{
                ledStatus.set(100,2000)
            }
        }
    }
}


// master request first 1byte, for the moment only one bool.
// if no Data, just send pong and time
// if buffer have data, master will request that time ID, TIME, (3bytes (ID) + 4bytes (uint32_t) + 1byte Checksum? + 1byte signal Strenght ? + ) 
void requestEvent() {
//   const uint8_t noData = 0x84; //pong 0x83, data 0x84
    uint8_t dataLength;
    uint8_t arrayToSend[12];
    uint32_t timeTemp = millis() - offsetTime;

    if (buf.isEmpty())
    {
        dataLength = 6;
        arrayToSend[0] = 0x83;
        arrayToSend[1] = 0x00; // need checksum function
        arrayToSend[2] = (timeTemp >> 24) & 0xFF;
        arrayToSend[3] = (timeTemp >> 16) & 0xFF;
        arrayToSend[4] = (timeTemp >> 8) & 0xFF;
        arrayToSend[5] = timeTemp & 0xFF;
    }
    else
    {
        struct Data e;
        uint32_t idTemp;
        dataLength = 10;
        arrayToSend[0] = 0x84;
        arrayToSend[1] = 0x00; // need checksum function

        buf.pull(&e);

        arrayToSend[2] = 0x00; //32bits proof compatible Receiver...
        arrayToSend[3] = (e.id >> 16) & 0xFF;
        arrayToSend[4] = (e.id >> 8) & 0xFF;
        arrayToSend[5] = e.id & 0xFF;

        arrayToSend[6] = (e.time >> 24) & 0xFF;
        arrayToSend[7] = (e.time >> 16) & 0xFF;
        arrayToSend[8] = (e.time >> 8) & 0xFF;
        arrayToSend[9] = e.time & 0xFF;
        arrayToSend[10] = e.signal;
    }

    Wire.write(arrayToSend, dataLength); //sizeof(arrayToSend) / sizeof(arrayToSend[0]));

}


// receive ping, init, or stop byte.
void receiveEvent(int howMany)
{
    uint8_t receivedByte;
    while(Wire.available()) 
    { 
        receivedByte = Wire.read(); // receive byte as a character
    }

    switch (receivedByte)
    {
    case 0x20: // start byte
        running = true;
        offsetTime = millis();
        break;
    
    case 0x30: // stop byte
        running = false;
        //purge CircularBuffer
        while(!buf.pull()){
            bool plop;
        }
        
        break;

    // case 0x10: // ping byte
    //     pong = true;
    //     break;

    default:
        break;
    }
}







//    if(flag == true)
//    {
//       long Ldata = (long)data[3]<<24 | (long)data[2]<<16 | (long)data[1]<<8 | (long)data[0];
//       Serial.println(Ldata, HEX); //send to serial monitor for testing purposes
//       flag =  false;
//    }