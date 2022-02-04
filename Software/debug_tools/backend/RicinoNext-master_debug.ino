#define RXD2 25
#define TXD2 26

#define IR_PIN_TEMP 27
void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("ready");
  pinMode(IR_PIN_TEMP, OUTPUT);

}

// 21 22 i2c
// 17 2 serial

void loop() {
  static uint32_t pulseIRdelay = 0;
  static bool pulseIRbool = false;
  
  if (Serial.available()) {      // If anything comes in Serial (USB),
    byte inByte = Serial.read();
    switch (inByte) {
    case 'T': // toggle pin 27
        digitalWrite(IR_PIN_TEMP, HIGH);
        pulseIRbool = true;
        pulseIRdelay = millis();
        break;

    case 'I': // toggle pin 27
        Serial2.println("I");
        break;
    
    case 'L': // toggle pin 27
        Serial2.println("L");
        break;
    
    case 'R': // toggle pin 27
        Serial2.println("R");
        break;

    case 'S':
        Serial2.println("S");
        break;

    case 'F':
        Serial2.println("F");
        break;

    
    }    
//    Serial1.write(Serial.read());   // read it and send it out Serial1 (pins 0 & 1)
  }


  if ( millis() - pulseIRdelay > 100 && pulseIRbool){
      digitalWrite(IR_PIN_TEMP, LOW);
  }

  if (Serial2.available() > 0) {     // If anything comes in Serial1 (pins 0 & 1)
    Serial.write(Serial2.read());   // read it and send it out Serial (USB)
  }
}