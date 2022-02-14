typedef struct  {
  byte slaveID; // slaveID
  byte cmdID; // commandID
  uint32_t ID; // 24bits id 
  uint8_t checksum; // and crc8.
  uint32_t mS;    // millis() from slave
  uint8_t reception; //0-255 to 0-100%
  uint8_t tempporary; //typo wait
  uint16_t temporary; //wait
} RemoteData_t;

typedef struct  {
  byte cmdID; // slaveID
  byte checksum; // commandID
  uint32_t mS;    // millis() from slave
} RemotePong_t;

#include <Wire.h>          // For I2C communication with panStamp, http://www.arduino.cc/en/Reference/Wire

#define PACKET_SIZE 14     // I2C packet size, slave will send 14 bytes to master
#define addrSlaveI2C 21    // ID of i2C slave

// Function prototype
bool getData(RemoteData_t* Info);
bool getPong(RemotePong_t* Info);

void setup () 
{
  Serial.begin(9600);
  Wire.begin();  // Initialiae wire library for I2C communication
  Wire.beginTransmission(addrSlaveI2C);
  Wire.write(0x8A);
  Wire.endTransmission();
}

RemotePong_t pong1;
RemoteData_t data1;

void loop()
{
    static uint8_t serialDisplayCounter = 0;
    static uint32_t pingLoopTimer = millis();
    const uint32_t pingLoopDelay = 100;
    uint8_t counter = 0;
    uint8_t pong = 0;
    byte i2CData[PACKET_SIZE]; 
    
    if (millis() - pingLoopTimer >= pingLoopDelay){ // >= to get more round number
        pingLoopTimer = millis();
        Wire.beginTransmission(addrSlaveI2C);
        Wire.write(0x80);
        Wire.endTransmission();

        Wire.requestFrom(addrSlaveI2C, 6);

        while(Wire.available())
        {
          i2CData[counter++] = Wire.read(); // receive a byte of data
          if (i2CData[0] == 0x81){
              pong = 1;
          }
          else if (i2CData[0] == 0x82){
              pong = 2;
          }
          else if (i2CData[0] == 0x83){
              pong = 3;
          }
        }
    }
    
    if (pong > 1){ // around 1ms
      
        pong1.cmdID = i2CData[0];
        pong1.checksum = i2CData[1];
        pong1.mS = ( ((uint32_t)i2CData[2] << 24)
                   + ((uint32_t)i2CData[3] << 16)
                   + ((uint32_t)i2CData[4] <<  8)
                   + ((uint32_t)i2CData[5] ) );

        uint8_t DataToCheck[] = {i2CData[2], i2CData[3], i2CData[4], i2CData[5]};
        uint8_t calcChecksum = CRC8(DataToCheck, sizeof(DataToCheck));

        if (serialDisplayCounter > 8){
            serialDisplayCounter = 0;
            Serial.print(pong1.cmdID);
            Serial.print("\t");
            Serial.print(pong1.checksum,HEX);
            Serial.print("\t");
            Serial.print(calcChecksum,HEX);
            Serial.print("\t");
            Serial.print(pong1.mS);
            Serial.println();
        } else {
            serialDisplayCounter++;
        }


        if (pong > 2){ // add 2ms
            Wire.beginTransmission(addrSlaveI2C);
            Wire.write(0x85);
            Wire.endTransmission();

            Wire.requestFrom(addrSlaveI2C, 14);    // request data from I2C slave

            counter = 0;
            while(Wire.available())    // Wire.available() will return the number of bytes available to read
            {
                i2CData[counter++] = Wire.read(); // receive a byte of data
            }

            data1.cmdID = i2CData[0];  // Slave address
            data1.slaveID = i2CData[1];  // some sort of command ID
        
            data1.ID = ( ((uint32_t)i2CData[2] << 24)
                       + ((uint32_t)i2CData[3] << 16)
                       + ((uint32_t)i2CData[4] <<  8)
                       + ((uint32_t)i2CData[5] ) ); 
            
            data1.checksum = i2CData[6]; //checksum
            
            // Get long integer - milliseconds
            data1.mS = ( ((uint32_t)i2CData[7] << 24)
                       + ((uint32_t)i2CData[8] << 16)
                       + ((uint32_t)i2CData[9] <<  8)
                       + ((uint32_t)i2CData[10] ) );  

            uint8_t DataToCheck[] = {i2CData[2], i2CData[3], i2CData[4], i2CData[5], i2CData[7], i2CData[8], i2CData[9], i2CData[10]};
            uint8_t calcChecksum = CRC8(DataToCheck, sizeof(DataToCheck));
        
            data1.reception = i2CData[11];

            if( calcChecksum == data1.checksum){      
                Serial.print(data1.slaveID);
                Serial.print("\t");
                Serial.print(data1.checksum,HEX);
                Serial.print("\t");
                Serial.print(calcChecksum,HEX);
                Serial.print("\t");
                Serial.print(data1.mS);
                Serial.print("\t");
                Serial.print(data1.ID,HEX);
                Serial.print("\t");
                Serial.print(data1.reception,HEX);
                Serial.println();
            }
            else
            {
                Serial.println("Error data reception");
            }
        }
    }
//  
//  RemoteData_t gate1;
//  if( getData( &gate1 ))
//  {
//    Serial.print(data1.slaveID);
//    Serial.print("\t");
//    Serial.print(data1.cmdID,HEX);
//    Serial.print("\t");
//    Serial.print(data1.ID,HEX);
//    Serial.print("\t");
//    Serial.print(data1.checksum,HEX);
//    Serial.print("\t");
//    Serial.print(data1.mS);
//    Serial.print("\t");
//    Serial.print(data1.reception,HEX);
//    Serial.println();
//  }
//  else
//  {
//    Serial.println(F("No packet received"));
//  }
// 
//  delay(2000);
}  // end loop()


//bool getPong(RemotePong_t* Info)
//{
//  
//}

// I2C Request data from slave
bool getData(RemoteData_t* Info)
{
  bool gotI2CPacket = false;    
  byte i=0;
  byte i2CData[PACKET_SIZE];  // don't use char data type
  
  Wire.requestFrom(addrSlaveI2C, PACKET_SIZE);    // request data from I2C slave
  
  while(Wire.available())    // Wire.available() will return the number of bytes available to read
  {
    i2CData[i++] = Wire.read(); // receive a byte of data
    gotI2CPacket = true;  // Flag to indicate sketch received I2C packet
  }

  // If we got an I2C packet, we can extract the values
  if(gotI2CPacket)
  {
    gotI2CPacket = false;  // Reset flag

    Info->slaveID = i2CData[0];  // Slave address
    Info->cmdID   = i2CData[1];  // some sort of command ID

    Info->ID = ( ((uint32_t)i2CData[3] << 16)
                     + ((uint32_t)i2CData[4] <<  8)
                     + ((uint32_t)i2CData[5] ) ); 
    
    Info->checksum   = i2CData[6]; //checksum
    
    // Get long integer - milliseconds
    Info->mS = ( ((uint32_t)i2CData[7] << 24)
                     + ((uint32_t)i2CData[8] << 16)
                     + ((uint32_t)i2CData[9] <<  8)
                     + ((uint32_t)i2CData[10] ) );  

    Info->reception = i2CData[11];
    
    return true;
  }  // end got packet
  else
  { return false; } // No Packet received
}

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
