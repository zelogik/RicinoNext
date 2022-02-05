//HardwareSerial Serial1(1);
//HardwareSerial Serial2(2); // GPIO 17: TXD U2  +  GPIO 16: RXD U2
//esp32
void setup() {
    Serial.begin(115200); // (USB + TX/RX) to check

    //UART_CONF0_REG  Configuration register 0
    //UART0: 0x3FF40020
    //UART1: 0x3FF50020
    //UART2: 0x3FF6E020

    WRITE_PERI_REG( 0x3FF6E020 , READ_PERI_REG(0x3FF6E020) | (1<<16 ) | (1<<9 ) );  //UART_IRDA_EN + UART_IRDA_DPLX  "Let there be light"
    //Serial.print("Reg: "); Serial.println(READ_PERI_REG(0x3FF6E020),BIN);  //For Debug only
}//setup

const uint8_t conf[] = {0x00, 0x02, 0x04, 0x06, 0x08, 0x0A,
                        0x0C, 0x0E, 0x20, 0x22, 0x24, 0x26,
                        0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32,
                        0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E};


void loop() {
  const uint32_t speedArray[] = {9600, 14400, 19200, 28800, 38400, 57600, 115200};
  static uint32_t slowLoopTimer = millis();
  const uint32_t slowLoopDelay = 1000;
  static uint8_t speedCounter = 0;
  static uint8_t confCounter = 0;

  if (millis() - slowLoopTimer > slowLoopDelay){
    Serial.println("");
    Serial2.end();
    delay(10);
    Serial2.begin(speedArray[speedCounter], conf[confCounter]);
    delay(10);
    slowLoopTimer = millis();

    if (confCounter + 1 < (sizeof(conf) / sizeof(conf[0])))
    {
      confCounter++;
    }
    else
    {
      if (speedCounter + 1 < (sizeof(speedArray) / sizeof(speedArray[0])))
      {
        speedCounter++;
        confCounter = 0;
      }
      else
      {
        speedCounter = 0;
      }
    }
    Serial.print(speedArray[speedCounter]);
    Serial.print(" ");
    Serial.print(conf[confCounter]);
  }
  
  while (Serial2.available()) {
      Serial.print( (char) Serial2.read() );
      delay(2);
  } //while
} //loop
