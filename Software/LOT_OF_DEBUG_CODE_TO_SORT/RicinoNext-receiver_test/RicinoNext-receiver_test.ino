#include <Wire.h>

#define GATE1_NUMBER 21

// pseudo algo
// master send ping every 200ms to each receiver?
// 0x80, ping
// 0x8A, start race
// 0x8F, stop race
// 0x85, ask data to receiver (if get 0x83)
//
// receiver responding by pong
// if receive 0x80:
// 0x81, pong ready // waiting 0x8A, receiver alive
// 0x82, pong, time // no data and running
// 0x83, pong, time // data to send
// if receive 0x85:
// 0x86, ID, time

class Receiver
{
private:
    int _gateAddress;
    const uint32_t timeOutDelay = 2000;     // milliseconds of timeout
    uint32_t timeOutTimer = millis();
    const uint32_t pingLoopDelay= 1000;
    uint32_t pingLoopTimer = millis();
    const uint32_t fastLoopDelay = 100;
    uint32_t fastLoopTimer = millis();
    uint32_t lastPing;    // milliseconds of off-time
    bool connected = false;
    bool needPong = false;
    bool needData = false;
    byte incommingData[14] = {0};
    bool newValue = false;

    
    uint8_t slaveID;  // Slave address
    uint8_t cmdID;  // some sort of command ID
    uint32_t ID; 
    uint8_t checksum; //checksum
    uint32_t mS;  
    uint8_t signal; 

    void pong(){
        byte receiveByte[6];
        uint8_t counter;
        Wire.requestFrom(_gateAddress, 6);
        while (Wire.available()) {
            receiveByte[counter++] = Wire.read();
        }
        
        // change to ternary condition ..?
        if (receiveByte[0] == 0x81){
            connected = false;
            needPong = false;
        }
        else if (receiveByte[0] == 0x82){
            needPong = false;
            needData = false;
        }
        else if (receiveByte[0] == 0x83)
        {
            needPong = false;
            needData = true;
        }
        // get time
    }

    void dataProcessing(){ //uint8_t *data){
        uint8_t counter;
        
        Wire.beginTransmission(_gateAddress);
        Wire.write(0x85);
        Wire.endTransmission();
        
        Wire.requestFrom(_gateAddress, 14);    // request data from I2C slave
        
        while(Wire.available())    // Wire.available() will return the number of bytes available to read
        {
          incommingData[counter++] = Wire.read(); // receive a byte of data
        }

        slaveID = incommingData[0];  // Slave address
        cmdID   = incommingData[1];  // some sort of command ID
        
        ID = ( ((uint32_t)incommingData[3] << 16)
                         + ((uint32_t)incommingData[4] <<  8)
                         + ((uint32_t)incommingData[5] ) ); 
        
        checksum   = incommingData[6]; //checksum
        
        // Get long integer - milliseconds
        mS = ( ((uint32_t)incommingData[7] << 24)
                         + ((uint32_t)incommingData[8] << 16)
                         + ((uint32_t)incommingData[9] <<  8)
                         + ((uint32_t)incommingData[10] ) );  
        
        signal = incommingData[11];   
    }


public:
    Receiver(uint8_t gateAddress)
    {
        this->_gateAddress = gateAddress;
        // pinMode(_gateNumber, INPUT);
    }

    bool isNewValue(){
        return newValue;
    }

//    uint8_t getValue(){
//        newValue = false;
//        return incommingData;
//    }

//    bool getState(){
//        return state;
//    }

    void setState(bool state){
        if (connected != state)
        {
            bool _state = state ? 0x8A : 0x8F;
            Wire.beginTransmission(_gateAddress);
            Wire.write(_state);
            Wire.endTransmission();
            if (state) //presume receiver is active just after the endTransmission
            {
                connected = true;
            }
        }
    }

    void loop(){

        if (connected){
            // when race running get time too... calculate the delta, and trigger an error?
            if (needPong){
                pong();
            }
            else if (needData){
                dataProcessing();
            }

//            if (millis() - fastLoopTimer > fastLoopDelay)
//            {
//                fastLoopTimer = millis();
//                Wire.requestFrom(_gateNumber, 12);
//                uint8_t counter = 0; 
//                while (Wire.available()) {
//                    incommingData[counter] = Wire.read();
//                    counter++;
//                }
//                dataProcessing();
//            }

            if (millis() - pingLoopTimer > pingLoopDelay){ // send ping every second
                Wire.beginTransmission(_gateAddress);
                Wire.write(0x80);
                Wire.endTransmission();
                needPong = true;
                pingLoopTimer = millis();
            }

            if (millis() - timeOutTimer > timeOutDelay){
                connected = false;
                timeOutTimer = millis();
            }
        }
    }
};

Receiver gate1 = Receiver(GATE1_NUMBER);
//Receiver gate2 = Receiver(GATE2_NUMBER);
//Receiver gate3 = Receiver(GATE3_NUMBER);

void setup()
{
  Serial.begin(9600);
  Wire.begin(); // join i2c bus (address optional for master)  
}

void loop()
{
    gate1.loop();
//    gate2.loop();
//    gate3.loop();
}
