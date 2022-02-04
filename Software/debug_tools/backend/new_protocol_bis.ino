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




// slave sender
#include <Wire.h>


uint8 buffer[10];
buffer[0] = 0x25;
buffer[1] = 0x75;

void setup() {
  Wire.begin(8);                // join i2c bus with address #8
  Wire.onRequest(requestEvent); // register event
}

void loop() {
  delay(100);
}

bool dataToSend = false;


// master request first 1byte, for the moment only one bool.
// if bool = true, master will request that time ID, TIME, (3bytes (ID) + 4bytes (uint32_t) + 1byte Checksum? + 1byte signal Strenght ? + ) 
void requestEvent() {
  const uint8_t noData = 0x51;
  if (!dataToSend)
  {
      Wire.write(noData; 
  }
  else
  {
      Wire.write
  }
  
  // as expected by master
}
int32_t bigNum = 12345678;
byte myArray[4];
myArray[0] = (bigNum >> 24) &amp; 0xFF;
myArray[1] = (bigNum >> 16) &amp; 0xFF;
myArray[2] = (bigNum >> 8) &amp; 0xFF;
myArray[3] = bigNum & 0xFF;
Wire.write(myArray, 4);


int16_t bigNum = 1234;
byte myArray[2]; 
myArray[0] = (bigNum >> 8) &amp; 0xFF;
myArray[1] = bigNum & 0xFF;
Wire.write(myArray, 2);
}



   if(flag == true)
   {
      long Ldata = (long)data[3]<<24 | (long)data[2]<<16 | (long)data[1]<<8 | (long)data[0];
      Serial.println(Ldata, HEX); //send to serial monitor for testing purposes
      flag =  false;
   }