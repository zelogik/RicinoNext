#include <Wire.h>

#define GATE1_NUMBER 1
#define GATE2_NUMBER 2
#define GATE3_NUMBER 3
#define WIRE Wire

// case 0x80: // start byte
// case 0x8F: // stop byte
// case 0x8A: // ping/pong byte
class Receiver
{
private:
    uint8_t _gateNumber;
    const uint32_t timeOutDelay = 2000;     // milliseconds of timeout
    uint32_t timeOutTimer = millis();
    const uint32_t pingLoopDelay = 1000;
    uint32_t pingLoopTimer = millis();
    const uint32_t fastLoopDelay = 100;
    uint32_t fastLoopTimer = millis();
    uint32_t lastPing;    // milliseconds of off-time
    bool connected = false;
    bool needPong = false;
    uint8_t *incommingData[12] = {0};
    bool newValue = false;
    

    void pong(){
        uint8_t receiveByte[6];
        uint8_t counter;
//        Wire.requestFrom(_gateNumber, 6);
        while (Wire.available()) {
            receiveByte[counter] = Wire.read();
            counter++; // maybe add a max counter value ...
        }
        
        // change to ternary condition ..?
        if (receiveByte == 0x83){
            needPong = false;
        }
        else
        {
            connected = false;
        }
    }


public:
    Receiver(uint8_t _gateNumber)
    {
        this->_gateNumber = _gateNumber;
        // pinMode(_gateNumber, INPUT);
    }

    bool isNewValue(){
        return newValue;
    }

    uint8_t getData(){
        newValue = false;
        return incommingData;
    }

    bool getState(){
        return connected;
    }

    void setState(bool state){
        if (connected != state)
        {
            bool _state = state ? 0x80 : 0x8F;
            Wire.beginTransmission(_gateNumber);
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

            if (millis() - fastLoopTimer > fastLoopDelay)
            {
                fastLoopTimer = millis();
                Wire.requestFrom((int)_gateNumber, 12);
                uint8_t counter = 0; 
                while (Wire.available()) {
                    incommingData[counter] = Wire.read();
                    counter++;
                }
                if (incommingData == 0x84)
                {
                    newValue = true;
                }
                else if (incommingData == 0x82)
                {
                    newValue = false;
                }
            }

            if (millis() - pingLoopTimer > pingLoopDelay){ // send ping every second
                Wire.beginTransmission((int)_gateNumber);
                Wire.write(0x8A);
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
Receiver gate2 = Receiver(GATE2_NUMBER);
Receiver gate3 = Receiver(GATE3_NUMBER);

void setup()
{
  Wire.begin(); // join i2c bus (address optional for master)
}

void loop()
{
    gate1.loop();
    gate2.loop();
    gate3.loop();
    uint8_t *data = gate1.getData();
    processingData(&data);
}

void processingData(uint8_t *data[]){
   for(uint8_t i = 0;  i < 12; i++)
   {
     Serial.print(*data[i]);
   }
}
