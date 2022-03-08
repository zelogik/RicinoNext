// Robitronic compatible attinyX4 transmitter

//#define __AVR_ATtinyX5__ // It seems like if you try to compile for ATtinyX5, the define is missing, so you'll have to set it manually
#include <EEPROM.h>

//example of timing...
// we could use an array of uint16_t for a almost perfect timing but the attiny will see his RAM burning
// 0, 10, 20, 30, 40, 50, 60, 70, 80, 90,   105, 115, 125, 135, 145, 155, 165, 175, 185, 195,   210, 220, 230, 240, 250, 260, 270, 280, 290, 300,   315, 325, 335, 345, 355, 365, 375, 385, 395, 405
// so we use NOP operation, 0.25us at 8mhz on avr architecture.
// #define NOP __asm__ __volatile__ ("nop\n\t") // finally less readable than nop\n\t


#define F_CPU 8000000UL // (really important?), arduino stack should set it

#if defined(__AVR_ATtinyX5__) || defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__) 
  #define LEDIR PB1
  #define LEDPIN PB0
  #define RESETPIN PB2 // reset pin only at start! need test...
  #define DDR_AB DDRB
  #define PIN_AB PINB
  #define PORT_AB PORTB
  #if defined (__AVR_ATtinyX5__)
    #define ATTINYCORE 1
  #endif
#elif defined(__AVR_ATtinyX4__) || defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  #define LEDIR PB2    // Physical Pin 6 (PB1) on ATtiny85 // PB2
  #define LEDPIN PA7   // Physical Pin 5 // PA7 // PB0
  #define RESETPIN PA6 // reset pin only at start! need test...
  #define DDR_AB DDRA
  #define PIN_AB PINA
  #define PORT_AB PORTA
  #if defined (__AVR_ATtinyX4__)
    #define ATTINYCORE 1
  #endif  
#endif

#define PERIOD_WAIT_START 6
#define PERIOD_WAIT_BIT 5 // for a complete loop of approx 10us
#define PERIOD_WAIT_NEXT_BYTE 10
#define ARRAY_ID_LEN 6 // 6 in case of 32bits compatibility

// define tx_id or let to 0 for a "true" random ID.
uint32_t txID = 0;
bool resetID = false; // set to true to force reset ID at "each" reboot, but you don't need that :)

// scheme is: [0] LSB 8bit ID + [1] 8Bit ID + [2] 8bit ID + [3] CheckSUM + [4] PARITY(XXXXX000)msb or lsb...
uint8_t arrayID[ARRAY_ID_LEN] = {};

void setup()
{
    // Serial.begin(9600); // debug, of course working only on bigger atmega

    DDR_AB &= ~(1 << RESETPIN);
    if ((PIN_AB & B01000000) == 1)
    {
        resetID = true;
    }

    setUniqueID();
    // delay(10); // why not, 68bits more
    // pinMode(LEDPIN, OUTPUT); // 100bits more...
    DDR_AB |= _BV(LEDPIN); // Blinking LED set to OUTPUT
    DDRB |= _BV(LEDIR); // IR LED set to OUTPUT
}

void loop()
{
    const uint8_t intervals[] = {15, 50, 20, 255}; // lookalike heartbeat pulsation
    const uint16_t rate = 100;
    static uint8_t state = 0;
    static uint16_t heartBeatLoop = 0;
    static uint16_t irLoop = 0;
    static uint16_t irDelay = random(80, 500);
    static bool fakeReadState = false;
    static bool lastFakeReadState = true;

    // IR pulse must been send with random delay (detect many differents IR pulse at receiver side)
    #if  defined(ATTINYCORE)
        delayMicroseconds(10); //add attinyCore compatibility
    #else
        __delay_us(10);
    #endif

    if (irLoop > irDelay)
    {
        irDelay = random(80, 500);
        codeLoop();
        irLoop = 0;
    }
    irLoop++;

    // like an heartbeat pulsation! but small one!
    if (heartBeatLoop > intervals[state] * rate)
    {
        if (state % 2 ? 1 : 0)
        {
            PORT_AB |= (1 << LEDPIN);
            fakeReadState = true;
        }
        else
        {
            PORT_AB &= ~(1 << LEDPIN);
            fakeReadState = false;
        }

        // digitalWrite(LEDPIN, state % 2 ? HIGH : LOW); //use PORTB... as digitalWrite use more than 120bits
        if ( fakeReadState != lastFakeReadState)
        {
          state++;
          lastFakeReadState = fakeReadState;
        }
        
        if (state == sizeof(intervals))
        {
            state = 0;
        }
        heartBeatLoop = 0;
    }
    heartBeatLoop++;
}

// Block the main loop for around 500us ... time to send the IRDA code.
void codeLoop()
{
    uint8_t parityMask = 0x08; //0b00001000 -> 6bits length MSB but LSB order if I remembering well
    uint8_t idMask;

    // 3x1Byte + 1Byte checksum,
    for (uint8_t i = 0; i < 4; i++)
    {                  // [0] to [2] + checksum
        idMask = 0x80; // 128 or 0b10000000
        pulse(true);   // start byte
        #if  defined(ATTINYCORE)
            delayMicroseconds(PERIOD_WAIT_START); //add attinyCore compatibility
        #else
            _delay_us(PERIOD_WAIT_START);
        #endif

        for (int j = 0; j < 8; j++)
        {
            pulse((arrayID[i] & idMask) ? 0 : 1); // inverse bit pulse
            idMask >>= 1;
            #if  defined(ATTINYCORE)
                delayMicroseconds(PERIOD_WAIT_BIT); //add attinyCore compatibility
            #else
                _delay_us(PERIOD_WAIT_BIT);
            #endif
        }

        pulse(arrayID[5] & parityMask); // parity bit for each Byte
        parityMask >>= 1;
        #if  defined(ATTINYCORE)
          delayMicroseconds(PERIOD_WAIT_START);
        #else
        _delay_us(PERIOD_WAIT_NEXT_BYTE); // approx IRDA time between 2 bytes...
        #endif
       // Serial.print("  ");
    }
    // Serial.println();
}

// Only needed to setting the EEPROM if txID not set
// but fill the arrayID with precalculated checksum and parity
void setUniqueID()
{
    //      if ( EEPROM.read(0) != 0xff )
    for (uint8_t i = 0; i < ARRAY_ID_LEN; i++)
    {
        arrayID[i] = EEPROM.read(i);
    }

    if (arrayID[5] == 0xFF || resetID) // checksum is not set OR reset
    {

        uint8_t checkID = EEPROM.read(3); // check CRC

        if (txID == 0)
        { // make a pseudo random txID
            randomSeed(analogRead(A1));
            for (uint8_t u = 0; u < 254; u++)
            {
                txID += *((byte *)u); //checksum += the byte number u in the ram
            }
            // Serial.println(txID);
        }

        // let calculate the checksum
        // And then the parity to LSB->MSB ?
        arrayID[3] = (uint8_t)(0x00 & 0xFF); // ((txID >> 24) & 0xFF); //almost 32bits proof compatible Receiver...
        arrayID[2] = (uint8_t)((txID >> 16) & 0xFF);
        arrayID[1] = (uint8_t)((txID >> 8) & 0xFF);
        arrayID[0] = (uint8_t)(txID & 0xFF);

        // Checksum
        arrayID[3] = getCRC8(arrayID, 3); //override the 32bits compatible oups...but easier for passing array to function

        // Parity
        arrayID[5] = getParity(arrayID, 4); // write all parity bits to Byte

        // Write/Save to EEPROM
        if (checkID != arrayID[3]) // don't overwrite EEPROM if CRC8 already the same...
        {
            for (uint8_t i = 0; i < ARRAY_ID_LEN; i++)
            {
                EEPROM.write(i, arrayID[i]);
            }
        }
    }
}

// Only needed to setting the EEPROM
uint8_t getParity(const uint8_t *data, uint8_t len)
{
    uint8_t parity = 0;

    for (uint8_t i = len; i > 0; i--)
    {
        uint8_t extract = *data;
        uint8_t parityBit = 1; // 1 for odd parity

        while (extract)
        {
            parityBit ^= extract & 1;
            extract >>= 1;
        }
        parity <<= 1;
        parity |= parityBit;
        data++;
    }
    return parity; // return lsb to msb
}

// next version of parity..? less readable
//uint8_t bitparity(const uint32_t data) {
//    // Return the parity of the given data
//    register uint32_t x = (data ^ (data >> 0x10));
//    x = (x ^ (x >> 0x8));
//    x = (x ^ (x >> 0x4));
//    return (uint8_t)((0x6996 >> (x & 0xF)) & 0x1);
//}

// inefficient CPU usage BUT ROM and RAM lover. Used only one time so perfect for our need. (around 24us for calculation)
uint8_t getCRC8(const uint8_t *data, size_t len)
{
    uint8_t remainder = 0;

    for (int i = 0; i < len; i++)
    {
        remainder ^= *data++;

        for (uint8_t j = 8; j > 0; j--)
        {
            if (remainder & (1 << 7))
            {
                remainder = (remainder << 1) ^ 0x07; // POLYNOMIAL for standard CRC-8
            }
            else
            {
                remainder = (remainder << 1);
            }
        }
    }

    return (remainder);
}

// Pulse or no pulse but with "almost" good time length
// Yes... an interrupt based pulse could be better... pull request kindly accepted :-)
void pulse(bool state)
{
    if (state)
    {
        PORTB |= (1 << LEDIR); //replaces digitalWrite(, HIGH);
        // Serial.print("1 ");
    }
    else
    {
        __asm__("nop\n\t"); // null PORTB manipulation compensation
        // Serial.print("0 ");
    }

    __asm__("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"); // get around 1,6us for complete loop, comply with IRDA, 3/16 bit pulse duration or 1.627us
    // __asm__("nop\n\t"); // 0.125us at 8Mhz

    if (state)
    {
        PORTB &= ~(1 << LEDIR); //replaces digitalWrite(, LOW);
    }
    else
    {
        __asm__("nop\n\t"); // null PORTB manipulation compensation
    }
}
