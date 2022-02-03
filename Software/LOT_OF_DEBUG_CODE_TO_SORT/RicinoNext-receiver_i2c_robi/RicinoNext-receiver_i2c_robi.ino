//API i2c result send:
// 0x83, pong with time, and receiver status.
// 0x84, pong with ID data

// receive Byte possible
// 0x8A, start byte
// 0x8F, stop Byte
// 0x80, ping Byte

// i2c send loop // RESET only if sure to have send ?
// if isReady == true
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
uint8_t I2C_ADDRESS; // need to be different for each board...

volatile uint32_t offsetTime; // it's time uC - start time
volatile bool messagePending = false;

uint32_t deltaOffsetGate = 0; // it's offsetTime vs RobiBridge
uint8_t idInfo[PACKET_SIZE] = {};
uint8_t loopTime = 0;


uint8_t setup_gate_id(){
  uint8_t id_gate = 0;
  const uint8_t i2c_address[] = {20,21,22,23,24,25,26,27,28};

  id_gate = digitalRead(GATE_ID_PIN1) ? 1 : 0;
  id_gate = digitalRead(GATE_ID_PIN2) ? id_gate + 2 : id_gate;
  id_gate = digitalRead(GATE_ID_PIN3) ? id_gate + 4: id_gate;

  return i2c_address[id_gate];
}


volatile enum RECEIVER_STATE { 
  DISCONNECTED, // check i2c or power on master, trying to auto-connect
  CONNECTED, // slowing status  blink led, don't send trigger to i2c, time zeroed, ping
  START, // steady status led, send trigger to i2c, time running, ping, go to RACE
  RACE, // waiting message Request essentially
//  REQUEST, // Send Message, back to RACE
  STOP // Back to CONNECTED
} receiverState = DISCONNECTED;


class Led // Main class for led blinking rate.
{
private:
    uint8_t _ledPin;
    uint32_t OnTime = 1000;     // milliseconds of on-time
    uint32_t OffTime = 1000;    // milliseconds of off-time
    bool ledState = HIGH;                 // ledState used to set the LED
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


void setup(void) {

  I2C_ADDRESS = setup_gate_id();

//  for (uint8_t i = 0 ; i < I2C_ADDRESS; i++){
//    digitalWrite(LED_PIN, HIGH);
//    delay(200);
//    digitalWrite(LED_PIN, LOW);
//    delay(500);
//  }
  
  Serial.begin(38400);
  SoftSerialDebug.begin(9600);
  // setup the software serial pins
//  pinMode(RX, INPUT);
//  pinMode(TX, OUTPUT); 

  pinMode(GATE_ID_PIN1, INPUT);
  pinMode(GATE_ID_PIN2, INPUT);
  pinMode(GATE_ID_PIN3, INPUT);


  Wire.begin(I2C_ADDRESS);    // join i2c bus with address #8
  Wire.onRequest(requestEvent); // register event  
  Wire.onReceive(receiveEvent); // register event
 
  ledStatus.set(200,500);
}

void loop(void) {
    uint32_t startLoopTime = micros();

    ledStatus.loop();
//    SoftSerialDebug.println(startLoopTime);
    switch (receiverState)
    {
    case DISCONNECTED:
        // mainly try to connect to display
        ledStatus.set(900,100);
        receiverState = CONNECTED;
        break;
    
    case CONNECTED:
        // discard RobiGate Serial input, WAITING, START ORDER
        purgeSerial();
        break;
    
    case START:
        // init RobiGate, init offsetTime, goto RACE
        resetGate(true);
        offsetTime = millis();
        receiverState = RACE;
        ledStatus.set(1000,1000);
        break;
    
    case RACE:
        // calcul deltaOffsetGate, processing RobitGate Serial
        processingGate();
        break;
    
    case STOP:
        // Stop RobiGate if possible, return to CONNECTED
        resetGate(false);
        ledStatus.set(900,100);
        receiverState = CONNECTED;
        break;
    
    default:
        break;
    }

    loopTime = loopTimeRefresh(startLoopTime);
}

// We don't need robiGate "seconds ping" when we don't run
void purgeSerial(){
    uint8_t havePurged = 0;
    while (Serial.available()){
        havePurged = Serial.available(); //SoftWareSerial don't show remaining Bytes, only true or false...
        Serial.read();   
        //use of .flush();       
    }
}

uint8_t loopTimeRefresh(uint32_t startTime){
    uint32_t stopLoopTime = micros();
    uint32_t calculation = stopLoopTime - startTime;

    // 205/2048, around 0.1, should be faster but 0,1% error
    calculation = ((calculation << 5) * 205L) >> 11;
    if (calculation > 253){
        calculation = 254;
    }
    else if (calculation < 1){
        calculation = 1;
    }
    return calculation;
}


// Processing messge of RobiGate only One at a time.
void processingGate(){
    static uint32_t whileTimeoutDelay = 10;
    
    if (Serial.available() && !messagePending) {
    idInfo[0] = Serial.read();
    
        for (uint8_t i = 1; i < idInfo[0]; i++){
            uint32_t whileTimeoutTimer = millis();
            // As a complete message depend of the length of the first byte, waiting next bit is a good solution, is case of being too fast...
            while(!Serial.available()){ // waiting loop for next data, block main loop but no much...
                delay(1);
                if( millis() - whileTimeoutTimer < whileTimeoutDelay){
                    // put some error text/info ?
                    break;
                }
            }
            idInfo[i] = Serial.read();
        }
        messagePending = true;
        serial2DebugOutput(idInfo, sizeof(idInfo));
    }

    // Update deltaOffsetGate
    if (messagePending && (idInfo[2] == 0x83)){
        uint32_t timeRobi = ( ((uint32_t)idInfo[6] << 24)
                            + ((uint32_t)idInfo[5] << 16)
                            + ((uint32_t)idInfo[4] <<  8)
                            + ((uint32_t)idInfo[3] ) );

        deltaOffsetGate = abs(timeRobi - offsetTime);
    }
}

// todo: need to find the stop byte array
void resetGate(bool state){ //0 = stop, 1= reset, stop?
    byte startByte[] = { 0x03, 0xB9, 0x01};
    byte resetByte[] = { 0x03, 0xB0, 0x02}; //start?
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
void requestEvent() {
    // uint8_t dataLength;
    uint8_t I2C_Packet[PACKET_SIZE];            // Array to hold data sent over I2C to main Arduino
    // uint32_t timeTemp = millis() - offsetTime;

    // Void serial buffer (0x83) is more important than respond to pong (0x82)
    if (messagePending){
        messagePending = false;

        I2C_Packet[0] = 0x84; // Code saying it's an ID info
        I2C_Packet[1] = I2C_ADDRESS; // need checksum function


        I2C_Packet[3] = idInfo[3]; // id
        I2C_Packet[4] = idInfo[4]; //
        I2C_Packet[5] = idInfo[5]; //
        I2C_Packet[6] = idInfo[6]; // should be 0x00 for a 24bits Ir code

        I2C_Packet[7] = idInfo[7]; // Reverse Order Decode needed for time in millisSeconds
        I2C_Packet[8] = idInfo[8]; //
        I2C_Packet[9] = idInfo[9]; //
        I2C_Packet[10] = idInfo[10]; //

        I2C_Packet[11] = idInfo[11]; // number Hit
        I2C_Packet[12] = idInfo[11]; // Strength


        uint8_t DataToSend[10]; // Id to Strenght for the CRC calculation
        
        for (uint8_t i = 0; i < 10; i++){
            DataToSend[i] = I2C_Packet[i + 3]; // just remove code, address, and checksum
        }
        
        I2C_Packet[2] = CRC8(DataToSend, sizeof(DataToSend)); // need checksum function

        // Clear idInfo Array.. too soon ?
        for (uint8_t i = 0; i < idInfo[0]; i++){
            idInfo[i] = 0;
        }
        
    }
    else{
        if (receiverState == RACE)
        {
//            timeTemp = millis() - offsetTime;
            I2C_Packet[0] = 0x83;
            I2C_Packet[1] = I2C_ADDRESS;

            I2C_Packet[3] = receiverState; // Better to direct assign the right number ..?!?!
            I2C_Packet[4] = deltaOffsetGate; // maybe pass to 2x 8bits...

            uint8_t bytesInBuffer = (64 - (uint8_t)Serial.available()) * 4; // default Serial buffer is 64bytes
            I2C_Packet[5] = bytesInBuffer;
            
            I2C_Packet[6] = loopTime;

            uint8_t DataToSend[] = {I2C_Packet[3], I2C_Packet[4], I2C_Packet[5], I2C_Packet[6]};
            I2C_Packet[2] = CRC8(DataToSend, sizeof(DataToSend)); // need checksum function
        }

    }
    Wire.write(I2C_Packet, PACKET_SIZE); //sizeof(arrayToSend) / sizeof(arrayToSend[0]));
}


// receive ping, init, or stop byte.
void receiveEvent(int howMany)
{
    uint8_t receivedByte[2]= {}; // only two Bytes now... 
    // uint32_t timeTemp;
    // uint8_t arrayToSend[6];

    uint8_t arrayIncrement = 0;
    while(Wire.available()) 
    { 
        receivedByte[arrayIncrement++] = Wire.read(); // receive byte as a character
    }

    switch (receivedByte[0])
    {
    case 0x8A: // start byte
        receiverState = START;
        break;
    
    case 0x8F: // stop byte
        receiverState = STOP;
        break;

//     case 0x80: // ping/pong byte
//        receiverState = REQUEST;
//        break;

    //  case 0x85:
    //     sendData = true;
    //     break;

    default:
        break;
    }
}


void serial2DebugOutput(uint8_t idInfoSerial[], uint8_t idInfoSize){
  
  uint32_t timeRobi = 0;
  uint32_t idRobi = 0;
  
  if (idInfoSerial[2] == 0x84){ // car pass
      timeRobi = ( ((uint32_t)idInfoSerial[10] << 24)
               + ((uint32_t)idInfoSerial[9] << 16)
               + ((uint32_t)idInfoSerial[8] <<  8)
               + ((uint32_t)idInfoSerial[7] ) );
      idRobi = ( ((uint32_t)idInfoSerial[6] << 24)
               + ((uint32_t)idInfoSerial[5] << 16)
               + ((uint32_t)idInfoSerial[4] <<  8)
               + ((uint32_t)idInfoSerial[3] ) );

      SoftSerialDebug.print("timer: ");
      SoftSerialDebug.print(timeRobi, DEC);
      SoftSerialDebug.print(" | ");
      SoftSerialDebug.print("#id: ");
      SoftSerialDebug.print(idRobi, DEC);
      SoftSerialDebug.print(" | ");
      SoftSerialDebug.print("hit : ");
      SoftSerialDebug.print(idInfoSerial[11], DEC);
      SoftSerialDebug.print(" | ");
      SoftSerialDebug.print("strength : ");
      SoftSerialDebug.print(idInfoSerial[12], DEC);
      SoftSerialDebug.println();
  }
  if (idInfo[2] == 0x83) // timer
  {
      timeRobi = ( ((uint32_t)idInfoSerial[6] << 24)
               + ((uint32_t)idInfoSerial[5] << 16)
               + ((uint32_t)idInfoSerial[4] <<  8)
               + ((uint32_t)idInfoSerial[3] ) );
      SoftSerialDebug.print("timer : ");
      SoftSerialDebug.print(timeRobi, DEC);
      SoftSerialDebug.print(" | ");
      SoftSerialDebug.print(Serial.available());
      SoftSerialDebug.println();
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
