#include <Wire.h>
#include <I2C_Anything.h>

#define SLAVE_ADDRESS 42
#define regAddr 0x10

volatile uint32_t value;

void setup()
{
  Serial.begin(9600);
//  Serial.println("Serial ready !");

  Wire.begin();
}

void loop()
{
//  Serial.println(regAddr, HEX);

  // Send the register address to read
  Wire.beginTransmission(SLAVE_ADDRESS);
  I2C_writeAnything(regAddr);
  Wire.endTransmission();

  // Send request
  Wire.requestFrom(SLAVE_ADDRESS, sizeof(value));
  I2C_readAnything(value);
  Serial.println(value);
  Serial.println("");
  
  delay(1000);
}
