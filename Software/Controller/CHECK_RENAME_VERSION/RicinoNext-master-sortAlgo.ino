// #include <EEPROM.h>

#define NUMBER_RACER 4 // backend should work with 4 to endless (memory limit) racer without problem, frontend is fixed to 4 players...
#define ARRAY_LENGTH(x)  ( sizeof( x ) / sizeof( *x ) )

const uint8_t addressAllGates[] = {21, 22, 23}; // Order: 1 Gate-> ... -> Final Gate!
#define NUMBER_GATES 3
#define NUMBER_PROTOCOL 3 // 0:serial, 1:ESP, 2:tft

// DEBUG value!!!!
const uint32_t idList[] = { 0x1234, 0x666, 0x1337, 0x2468}; // 0x4321, 0x2222, 0x1111, 0x1357};
uint16_t idListTimer[] = { 2000, 2050, 2250, 2125}; // , 2050, 2150, 2250, 2350}; // used for the first lap!
uint32_t idListLastMillis[] = { 0, 0, 0, 0}; // , 0, 0, 0, 0,};
uint8_t idGateNumber[] = { 20, 20, 20, 20}; // , 19, 19, 19, 19};
// bool idChanged = false;

typedef enum {
  WAIT,          // Set parameters(Counter BIP etc...), before start counter
  BEEP,          // Only run beep before gotoSTART
  START,        // Racer can register, timeout after 20s, to to RACE
  RACE,         // update dataRace().
  FINISH,         // 1st player have win, terminate after 20s OR all players have finished.
  STOP         // finished auto/manual, reset every backend parameters
} Race_State;

Race_State raceState = WAIT;

typedef struct {
  public:
    // get info from i2c/gate
    uint32_t ID;
    uint32_t lapTime;
    uint8_t currentGate;

    // Calculated value:
    uint16_t laps;
//    uint32_t offsetLap;
    uint32_t bestLapTime, meanLapTime, lastLapTime, lastTotalTime;
  
    uint32_t lastTotalCheckPoint[NUMBER_GATES];
    uint32_t lastCheckPoint[NUMBER_GATES];
    uint32_t bestCheckPoint[NUMBER_GATES];
    uint32_t meanCheckPoint[NUMBER_GATES];
    uint32_t sumCheckPoint[NUMBER_GATES]; // 1193Hours for a buffer overflow?

    // todo
    int8_t statPos;
    uint8_t lastPos;
    bool haveMissed;
    uint8_t lastGate;
    bool haveInitStart = 0;
    uint8_t indexToRefresh; 
    bool positionChange[NUMBER_PROTOCOL]; // serial, tft, web,... add more

    bool haveUpdate[NUMBER_PROTOCOL][NUMBER_GATES];
    // bool needTFTUpdate[NUMBER_GATES];
    // bool needSerialUpdate[NUMBER_GATES]; // to update only changed gate

  void reset(){
    ID, laps = 0;
    lapTime, lastTotalTime, bestLapTime, meanLapTime, lastLapTime = 0;
    currentGate = addressAllGates[0];

    memset(positionChange, 0, sizeof(positionChange)); // serial, tft, web,... add more
    memset(haveUpdate, 0, sizeof(haveUpdate)); // false?

    memset(lastTotalCheckPoint, 0, sizeof(lastTotalCheckPoint));
    memset(lastCheckPoint, 0, sizeof(lastCheckPoint));
    memset(bestCheckPoint, 0, sizeof(bestCheckPoint));
    memset(meanCheckPoint, 0, sizeof(meanCheckPoint));
    memset(sumCheckPoint, 0, sizeof(sumCheckPoint));
  }

  void updateTime(uint32_t timeMs, uint8_t gate){
    currentGate = gate;
    lapTime = timeMs;
//    uint32_t tmp_lastTotalLapTime = totalLapTime;
    uint8_t idxGates = gateIndex(gate);

    if (haveInitStart){
      // one complete lap done
      if (currentGate == addressAllGates[0]){
         laps++;
         // Calculation of full lap!
         lastCalc(&lastLapTime, &lastTotalTime, NULL, timeMs);
         meanCalc(&meanLapTime, NULL, timeMs);
         bestCalc(&bestLapTime, lastLapTime);
      }

      // Calculation of check point passage
      uint8_t prevIndex = (idxGates == 0) ? ((uint8_t)NUMBER_GATES - 1) : (idxGates - 1);
  
      for (uint8_t i = 0; i < NUMBER_PROTOCOL; i++)
      {
        haveUpdate[i][idxGates] = true;
      }
      lastCalc(&lastCheckPoint[idxGates], &lastTotalCheckPoint[idxGates], &lastTotalCheckPoint[prevIndex], timeMs);
      meanCalc(&meanCheckPoint[idxGates], &sumCheckPoint[idxGates], lastCheckPoint[idxGates]);
      bestCalc(&bestCheckPoint[idxGates], lastCheckPoint[idxGates]);      
    }
    else
    {
      haveInitStart = true;
      // initOffset()?
    }
  }

  // todo
  void updateRank(uint8_t currentPosition){
    statPos = lastPos - currentPosition;
    lastPos = currentPosition;
  }

// serial, tft, web,... add more
  bool needGateUpdate(uint8_t protocol){
    bool boolUpdate = false;
    for (uint8_t i = 0; i < NUMBER_GATES; i++)
    {
      if (haveUpdate[protocol][i] == true)
      {
        haveUpdate[protocol][i] = false;
        indexToRefresh = i;
        boolUpdate = true;
        break; // don't need to check for any other change, will be done next time
      }
    }
    return boolUpdate;
  }

  private:
    uint8_t gateIndex(uint8_t gate){
      uint8_t idx;
      for (uint8_t i = 0; i < NUMBER_GATES; i++)
      {
        if (gate == addressAllGates[i])
        {
            idx = i;
            break; // idx found!
        }
      }
      return idx;
    }

    void bestCalc(uint32_t* best_ptr, uint32_t lastTime){
        if (lastTime < *best_ptr || *best_ptr == 0)
        {
            *best_ptr = lastTime;
        }
    }

    void meanCalc(uint32_t* mean_ptr, uint32_t* sum_ptr, uint32_t fullTime){
        uint32_t sumValue;
        if (sum_ptr == nullptr)
        { // full lap
            sumValue = fullTime;
        }
        else
        { // checkpoint
            *sum_ptr = *sum_ptr + fullTime;
            sumValue = *sum_ptr;
        }
        // maybe remove division and change with bitwise and multiplication.
        *mean_ptr = sumValue / ((laps > 0) ? laps : 1);
    }

    void lastCalc(uint32_t* last_ptr, uint32_t* lastTotal_ptr, uint32_t* lastPrevTotal_ptr, uint32_t fullTime){
        uint32_t value;
        if (lastPrevTotal_ptr == nullptr)
        { // full laps
          *last_ptr = fullTime - *lastTotal_ptr;
        }
        else
        { // checkpoint
          *last_ptr  = fullTime - *lastPrevTotal_ptr;
        }
        *lastTotal_ptr = fullTime;

        //  0   4   7       10  13  16
        //  |   |   |   ->  |   |   |
    }

} ID_Data_sorted;

ID_Data_sorted idData[NUMBER_RACER + 1]; // number + 1, [0] is the tmp for rank change, and so 1st is [1] and not [0] and so on...
// ID_Data idDataTmp;

// Sort-Of simple buffer
typedef struct {
    uint32_t ID = 0;
    uint8_t gateNumber = 0;
    uint32_t totalLapsTime = 0; // in millis ?
    bool isNew = false; //
} ID_buffer;

ID_buffer idBuffer[NUMBER_RACER];


void setup() {
  Serial.begin(9600);
  for (uint8_t i = 0 ; i < (NUMBER_RACER + 1); i++ )
  {
    idData[i].reset();
  }
  pinMode(LED_BUILTIN, OUTPUT);

}


void loop() {
    fakeIDtrigger(millis()); // only used for debug...
    sortIDLoop(); // processing ID.
//    serialAsyncPrint(millis());
    WriteSerial(millis());
    ReadSerial();

  // WriteESP();
  // ReadESP();

  // WriteTFT();
  // ReadTFT();
}

// Function below get called at each triggered gate.
void bufferingID(int ID, uint8_t gate, int totalTime){

  for (uint8_t i = 0; i < NUMBER_RACER; i++)
  {
    if (idBuffer[i].ID == ID)
    {
      idBuffer[i].totalLapsTime = totalTime;
      idBuffer[i].gateNumber = gate;
      idBuffer[i].isNew = true;
      break; // Only one ID by message, end the loop faster
    }
    else if (idBuffer[i].ID == 0)
    {
      idBuffer[i].ID = ID;
      idBuffer[i].totalLapsTime = totalTime;
      idBuffer[i].gateNumber = gate;
      idBuffer[i].isNew = true;
      for (uint8_t j = 1; j < NUMBER_RACER + 1; j++)
      {
          if ( idData[j].ID == 0)
          {
              idData[j].ID = idBuffer[i].ID;
              break; // Only registered the first Null .ID found
          }
      }
      break; // Only one ID by message, end the loop faster
    }
  }
}


// The most important loop sorting struct/table and put idBuffer -> idData.
void sortIDLoop(){
  // check if new data available
  for (uint8_t i = 0; i < NUMBER_RACER ; i++)
  {
    if (idBuffer[i].isNew == true)
    {
       idBuffer[i].isNew = false;
       // look ID current position
       uint8_t currentPosition = 0;
       uint32_t currentTime = idBuffer[i].totalLapsTime;
       uint8_t currentGate = idBuffer[i].gateNumber;
       

       // get current position
       for (uint8_t j = 1; j < NUMBER_RACER + 1 ; j++)
       {
         if ( idBuffer[i].ID == idData[j].ID )
         {
            currentPosition = j;
            idData[currentPosition].updateTime(currentTime, currentGate);
            break; // Processing only one idBuffer at a time!
         }
       }
      //  check if ID lowering position/rank.
       for (uint8_t k = currentPosition; k > 1 ; k--)
       {
          if (idData[k].laps > idData[k - 1].laps)
          {
              for (uint8_t l = 0; l < NUMBER_PROTOCOL; l++)
              {
                idData[k].positionChange[l] = true; // enable change for all protocols.
                idData[k - 1].positionChange[l] = true;
              }

              idData[0] = idData[k - 1]; // backup copy
              idData[k - 1] = idData[k];
              idData[k] = idData[0];

          }
          else
          {
            break; // Useless to continu for better position
          }
       }
    }
  }
}


//
void WriteESP(uint32_t ms){

  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++){
    if (idData[i].positionChange[1] == true || idData[i].needGateUpdate(1))
    {
      idData[i].positionChange[0] = false;

      for (uint8_t k = 0; k < 15 ; k++){
        Serial.println();
      }
      for (uint8_t j = 1; j < (NUMBER_RACER + 1) ; j++){
        Serial.print(j);
        Serial.print(F(" |ID: "));
        Serial.print(idData[j].ID);
      }
      break; // don't flood, only one message at a time!
    }
  }

}

// When you have only serial available for racing...
void WriteSerial(uint32_t ms){
  static uint32_t lastMillis = 0;
  const uint32_t delayMillis = 1000;
  
  if (ms - lastMillis > delayMillis)
  {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastMillis = millis();
  }

  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++){
    if (idData[i].positionChange[0] == true || idData[i].needGateUpdate(0))
    {
      idData[i].positionChange[0] = false;

      for (uint8_t k = 0; k < 15 ; k++){
        Serial.println();
      }
      for (uint8_t j = 1; j < (NUMBER_RACER + 1) ; j++){
        Serial.print(j);
        Serial.print(F(" |ID: "));
        Serial.print(idData[j].ID);
        Serial.print(F(" |laps: "));
        Serial.print(idData[j].laps);
        Serial.print(F(" |Time: "));
        Serial.print(millisToMSMs(idData[j].lastTotalTime));
        Serial.print(F(" |last: "));
        Serial.print(millisToMSMs(idData[j].lastLapTime));
        Serial.print(F(" |best: "));
        Serial.print(millisToMSMs(idData[j].bestLapTime));
        Serial.print(F(" |mean: "));
        Serial.print(millisToMSMs(idData[j].meanLapTime));
        Serial.print(F(" |1: "));
        Serial.print(millisToMSMs(idData[j].lastCheckPoint[1]));
        Serial.print(F(" |2: "));
        Serial.print(millisToMSMs(idData[j].lastCheckPoint[2]));
        Serial.print(F(" |3: "));
        Serial.print(millisToMSMs(idData[j].lastCheckPoint[0]));
        Serial.println();
      }
      break; // don't flood, only one message at a time!
    }
  }
}

void ReadSerial(){
    if (Serial.available()) {
    
    byte inByte = Serial.read();

        switch (inByte) {
        case 'S': // init timer
            raceState = START;
            break;

        case 'R': // End connection
            raceState = STOP;
            break;

        default:
            break;
        }
    }
}

// Use mainly for debug...
void serialAsyncPrint(uint32_t ms){
  static uint32_t lastMillis = 0;
  const uint32_t delayMillis = 1000;
  
  if (ms - lastMillis > delayMillis)
  {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastMillis = millis();
  }

  for (uint8_t i = 1; i < (NUMBER_RACER + 1) ; i++){
    if (idData[i].positionChange[0] == true)
    {
      Serial.print(i);
      Serial.print(" | laps: ");
      Serial.print(idData[i].laps);
      Serial.print(" | last: ");
      Serial.print(idData[i].lastLapTime);
      Serial.print(" | best: ");
      Serial.print(idData[i].bestLapTime);
      Serial.print(" | mean: ");
      Serial.print(idData[i].meanLapTime);
      Serial.println(" | full line change");
      idData[i].positionChange[0] = false;
      break; // don't flood, only one message at a time!
    }
    if (idData[i].needGateUpdate(0))
    {
      uint8_t gateChange = idData[i].indexToRefresh;
      Serial.print(i);
      Serial.print(" | ID: ");
      Serial.print(idData[i].ID);
      Serial.print(" | laps: ");
      Serial.print(idData[i].laps);
      Serial.print(" | ");
//      Serial.print(idData[i].totalCheckPoint[gateChange]);
      Serial.println(" | Gate update");
      break; // don't flood, only one message at a time!
    }
  }
}

// Debug Loop, simulate the gates
void fakeIDtrigger(int ms){
  uint8_t gate = 0;
    for (uint8_t i = 0; i < NUMBER_RACER; i++)
    {
        if (ms - idListLastMillis[i] > idListTimer[i])
        {
            idListLastMillis[i] = ms;
            idListTimer[i] = random(1500, 3300);

            uint8_t gateNumber_tmp = idGateNumber[i] + 1;
            gate = (gateNumber_tmp < 24) ? gateNumber_tmp : addressAllGates[0];
            idGateNumber[i] = gate;
            // Serial.print("fake ID: ");
            // Serial.print(idList[i]);
            // Serial.print(" | gate: ");
            // Serial.print(gate);
            // Serial.print(" | millis: ");
            // Serial.println(ms);

            // It's the main feature of that function!
            bufferingID(idList[i], gate, ms);
        }
    }
}


// todo: replace String to char! as 00:00.000 is well define
// char millisToMSMs(uint32_t tmpMillis){
String millisToMSMs(uint32_t tmpMillis){
    uint32_t millisec;
    uint32_t total_seconds;
    uint32_t total_minutes;
    uint32_t seconds;
    String DisplayTime;

    total_seconds = tmpMillis / 1000;
    millisec  = tmpMillis % 1000;
    seconds = total_seconds % 60;
    total_minutes = total_seconds / 60;

    String tmpStringMillis;
    if (millisec < 100)
      tmpStringMillis += '0';
    if (millisec < 10)
      tmpStringMillis += '0';
    tmpStringMillis += millisec;

    String tmpSpringSeconds;
    if (seconds < 10)
      tmpSpringSeconds += '0';
    tmpSpringSeconds += seconds;

    String tmpSpringMinutes;
    if (total_minutes < 10)
      tmpSpringMinutes += '0';
    tmpSpringMinutes += total_minutes;
    
    DisplayTime = tmpSpringMinutes + ":" + tmpSpringSeconds + "." + tmpStringMillis;

    return DisplayTime;
}

char timeToString(uint32_t tmpMillis){
  
  char str[10] = "";
  unsigned long nowMillis = tmpMillis;
  uint32_t tmp_seconds = nowMillis / 1000;
  uint32_t seconds = tmp_seconds % 60;
  uint16_t ms = nowMillis % 1000;
  uint32_t minutes = tmp_seconds / 60;
  snprintf(str, 10, "%02d:%02d.%03d", minutes, seconds, ms);
  return *str;


    //  timeToString(str, sizeof(str));
    // Serial.println(str); 
}