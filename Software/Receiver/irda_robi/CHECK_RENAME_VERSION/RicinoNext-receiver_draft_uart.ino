// TODO: Add Checksum verification...
#include <SoftwareSerial.h>

#define RX 9 //PIN_PB1 //8; // PB1
#define TX 8 // PIN_PB0 //9; // PB0

// create SoftwareSerial objects
SoftwareSerial SoftSerialDebug(RX,TX); // got 64Bytes of buffer that we will use mainly, buffer overflow... normally not, need more than 5cars in more than 300ms...

#define ID_INFO_LENGTH 13
#define LED_PIN  13//PIN_PD2


class Led // Main class for led blinking rate.
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


void setup(void) {
  Serial.begin(9600);
  SoftSerialDebug.begin(38400);
  // setup the software serial pins
//  pinMode(RX, INPUT);
//  pinMode(TX, OUTPUT);
//  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // inverse logic ?
  ledStatus.set(1000,1000);

}

void loop(void) {
//  uint16_t loopDelay = 2000;
//  static uint16_t loopTimer = millis();

  static uint8_t idInfo[ID_INFO_LENGTH] = {};
  static bool messagePending = false;
  static uint32_t whileTimeoutDelay = 10;
  static bool stateRace = false;

  ledStatus.loop();
  // Read only ONE message, let the Serial buffer keep others messages...
  if(SoftSerialDebug.available() && !messagePending && stateRace) {
      idInfo[0] = SoftSerialDebug.read();
    
      for (uint8_t i = 1; i < idInfo[0]; i++){
          uint32_t whileTimeoutTimer = millis();
          // As a complete message depend of the length of the first byte, waiting next bit is a good solution, is case...
          while(!SoftSerialDebug.available()){ // waiting loop for next data, block main loop but no much...
              delay(1);
              if( millis() - whileTimeoutTimer < whileTimeoutDelay){
                  break;
              }
          }
          idInfo[i] = SoftSerialDebug.read();
      }
      messagePending = true;
  }

  uint32_t timeRobi = 0;
  uint32_t idRobi = 0;
  static uint16_t delayBuffer = 100;
  
  if ( messagePending){
      if (idInfo[2] == 0x84){ // car pass
          timeRobi = ( ((uint32_t)idInfo[10] << 24)
                   + ((uint32_t)idInfo[9] << 16)
                   + ((uint32_t)idInfo[8] <<  8)
                   + ((uint32_t)idInfo[7] ) );
          idRobi = ( ((uint32_t)idInfo[6] << 24)
                   + ((uint32_t)idInfo[5] << 16)
                   + ((uint32_t)idInfo[4] <<  8)
                   + ((uint32_t)idInfo[3] ) );

          Serial.print("timer: ");
          Serial.print(timeRobi, DEC);
          Serial.print(" | ");
          Serial.print("#id: ");
          Serial.print(idRobi, DEC);
          Serial.print(" | ");
          Serial.print("hit : ");
          Serial.print(idInfo[11], DEC);
          Serial.print(" | ");
          Serial.print("strength : ");
          Serial.print(idInfo[12], DEC);
          Serial.println();
      }
      if (idInfo[2] == 0x83) // timer
      {
          timeRobi = ( ((uint32_t)idInfo[6] << 24)
                   + ((uint32_t)idInfo[5] << 16)
                   + ((uint32_t)idInfo[4] <<  8)
                   + ((uint32_t)idInfo[3] ) );
          Serial.print("timer : ");
          Serial.print(timeRobi, DEC);
          Serial.print(" | ");
          Serial.print(Serial.available());
          Serial.println();
      }

      // emptying the array
      uint8_t bufferLoop = idInfo[0];
      
      for (uint8_t i = 0; i < bufferLoop; i++){
//          SoftSerialDebug.print(idInfo[i]);
//          SoftSerialDebug.print(i);
          idInfo[i] = 0;
      }
      messagePending = false;
      delay(delayBuffer);
  }

  //purge function, buffer overflow check
  if (!stateRace){
//      ledBlink(); //debug function
      uint8_t havePurged = 0;
      while (SoftSerialDebug.available()){
          havePurged = SoftSerialDebug.available(); //SoftWareSerial don't show remaining Bytes, only true or false...
          SoftSerialDebug.read();
      }
//      if (havePurged){
//          SoftSerialDebug.print("Purge Buffer Bytes : ");
//          SoftSerialDebug.println(havePurged);
//      }
  }

  // read from port 0, send to port 1:
    if (Serial.available()) {
    
        byte inByte = Serial.read();
        byte startByte[] = { 0x03, 0xB9, 0x01};
        byte resetByte[] = { 0x03, 0xB0, 0x02};

        Serial.print("plop");
        switch (inByte) {
        case 'I': // init timer
            SoftSerialDebug.write(startByte, sizeof(startByte));
            stateRace = false;
            ledStatus.set(1000,1000);
            break;

        case 'R': // End connection
            SoftSerialDebug.write(resetByte, sizeof(resetByte));
            Serial.println("Reset");
            stateRace = true;
            ledStatus.set(100,900);
            break;

        case 'S': // test buffer overflow
            delayBuffer = 1500;
            break;

        case 'F':
            delayBuffer = 100;
            break;
        
//        case 'L':
//            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
//            break;     
        }
  }
}

void ledBlink(){
  static uint32_t previousMillis = 0;
  static bool ledState = LOW;
  const uint16_t interval = 1000;
  uint32_t currentMillis = millis();
  static uint32_t counter = 0;

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    ledState = ledState ? LOW : HIGH;

//    digitalWrite(LED_PIN, ledState);
    counter++;
    Serial.println(counter);
  }
}

// CRC8-maxim, change with CRC-8 standard, same than the IRDA transmitter... ?
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
