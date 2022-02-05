#include <Wire.h>
#include <I2C_Anything.h>

#define MY_ADDRESS 42
#define regAddr 0x10

//const float data = 20.05;
uint32_t counter = 0;
volatile byte check;

void setup()
{
//  Serial.begin (9600);

  Wire.begin (MY_ADDRESS);
  Wire.onReceive (receiveEvent);
  Wire.onRequest (requestEvent);
}

void loop()
{
  counter++;
  delayMicroseconds(1);
//  Serial.println(counter);
}

void receiveEvent(int howMany) {
//  Serial.println(howMany);
  if (howMany == 1) {
    I2C_readAnything(check);
  }
}

void requestEvent() {
//  Serial.println(add);
//  if(check == regAddr) {
    I2C_writeAnything(counter);
//  }
}
