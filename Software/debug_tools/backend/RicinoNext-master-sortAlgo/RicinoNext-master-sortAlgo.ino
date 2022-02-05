#define NUMBER_RACER 4 //test New
//and here
#define ARRAY_LENGTH(x)  ( sizeof( x ) / sizeof( *x ) )

const uint8_t addressAllGates[] = {21, 22, 23}; // Order: 1 Gate-> ... -> Final Gate!
const uint8_t NUMBER_GATES = ARRAY_LENGTH(addressAllGates);

const uint32_t idList[] = { 0x1234, 0x6666, 0x1337, 0x2468, 0x4321, 0x2222, 0x1111, 0x1357};
uint16_t idListTimer[] = { 6000, 6100, 6200, 6300, 2050, 2150, 2250, 2350};
uint32_t idListLastMillis[] = { 0, 0, 0, 0, 0, 0, 0, 0,};
uint8_t idGateNumber[] = { 19, 19, 19, 19, 19, 19, 19, 19};
// bool idChanged = false;

typedef struct {
  public:
    // get info from i2c/gate
    uint32_t ID;
    uint8_t currentGate;
    uint32_t totalLapTime;

    // Calculated value:
    // uint32_t lastLapsTime; // in millis ?
    uint16_t laps;
//    uint32_t offsetLap;
    uint32_t lastLapTime;
    uint32_t bestLapTime;
    uint32_t meanLapTime;
  
    uint32_t totalCheckPoint[NUMBER_GATES];
    uint32_t currentCheckPoint[NUMBER_GATES];
    uint32_t lastCheckPoint[NUMBER_GATES];
    uint32_t bestCheckPoint[NUMBER_GATES];
    uint32_t meanCheckPoint[NUMBER_GATES];
    uint32_t sumCheckPoint[NUMBER_GATES];

    // todo
    int8_t statPos;
    uint8_t lastPos;
    bool haveMissed;
    uint8_t lastGate;

    uint8_t indexToChange; 
    bool positionChange[3]; // serial, tft, web,... add more
    bool needWebUpdate[NUMBER_GATES];
    bool needTFTUpdate[NUMBER_GATES];
    bool needSerialUpdate[NUMBER_GATES]; // to update only changed gate

  void reset(){
    ID, laps = 0;
    totalLapTime, lastLapTime, bestLapTime, meanLapTime = 0;
    currentGate = addressAllGates[0];

    memset(positionChange, 0, sizeof(positionChange)); // serial, tft, web,... add more
    memset(needSerialUpdate, 0, sizeof(needSerialUpdate)); // false?
    memset(needWebUpdate, 0, sizeof(needWebUpdate)); // false?
    memset(needTFTUpdate, 0, sizeof(needTFTUpdate)); // false?

    memset(totalCheckPoint, 0, sizeof(totalCheckPoint));
    memset(currentCheckPoint, 0, sizeof(currentCheckPoint));
    memset(lastCheckPoint, 0, sizeof(lastCheckPoint));
    memset(bestCheckPoint, 0, sizeof(bestCheckPoint));
    memset(meanCheckPoint, 0, sizeof(meanCheckPoint));
    memset(sumCheckPoint, 0, sizeof(sumCheckPoint));
  }

  void updateTime(uint32_t time, uint8_t gate){
    currentGate = gate;
    uint32_t tmp_lastTotalLapTime = totalLapTime;
    uint8_t idxGate = gateIndex();
    uint8_t prevIndex = (idxGate == 0) ? (uint8_t)NUMBER_GATES : (idxGate - 1);

    needSerialUpdate[idxGate], needWebUpdate[idxGate], needTFTUpdate[idxGate] = true;

    totalCheckPoint[idxGate] = time;
    currentCheckPoint[idxGate] = totalCheckPoint[idxGate] - totalCheckPoint[prevIndex];
    lastCheckPoint[idxGate] = currentCheckPoint[idxGate] - currentCheckPoint[prevIndex];
    sumCheckPoint[idxGate] = sumCheckPoint[idxGate] + currentCheckPoint[idxGate];
    meanCheckPoint[idxGate] = sumCheckPoint[idxGate] / ((laps > 0) ? laps : 1);
    bestCheckPoint[idxGate] = (lastCheckPoint[idxGate] < bestCheckPoint[idxGate] || bestCheckPoint[idxGate] )
                            ? lastCheckPoint[idxGate] : bestCheckPoint[idxGate];
  }

  // todo
  void updateRank(uint8_t currentPosition){
    statPos = lastPos - currentPosition;
    lastPos = currentPosition;
  }

  bool updateCompleteLap(){
  }

  bool haveSerialUpdate(){
    bool boolUpdate = false;
    for (uint8_t i = 0; i < NUMBER_GATES; i++){
      if (needSerialUpdate[i] == true){
        needSerialUpdate[i] = false;
        indexToChange = i;
        boolUpdate = true;
        break; // don't need to check for any other change, will be done next time
      }
    }
    return boolUpdate;
  }

  bool haveWebUpdate(){
    bool boolUpdate;
    for (uint8_t i = 0; i < NUMBER_GATES; i++){
      if (needWebUpdate[i] == true){
        boolUpdate = true;
        break; // don't need to check for any other change, will be done next time
      }
    }
    return boolUpdate;
  }

  bool haveTFTUpdate(){
    bool boolUpdate;
    for (uint8_t i = 0; i < NUMBER_GATES; i++){
      if (needTFTUpdate[i] == true){
        boolUpdate = true;
        break; // don't need to check for any other change, will be done next time
      }
    }
    return boolUpdate;
  }

  private:
    // bool needUpdate[3] = {false}; // serial, tft, web,... add more

    uint8_t gateIndex(){
      uint8_t idx;
      for (uint8_t i = 0; i < NUMBER_GATES; i++){
        idx = (currentGate == addressAllGates[i]) ? idx : (idx + 1);
      }
      return idx;
    }

} ID_Data_sorted;

ID_Data_sorted idData[NUMBER_RACER + 1]; // number + 1, [0] is the tmp for rank change
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
  for (uint8_t i = 0 ; i < (NUMBER_RACER + 1); i++ ){
    idData[i].reset();
  }
}


void bufferingID(int ID, uint8_t gate, int totalTime){

  for (uint8_t i = 0; i < NUMBER_RACER; i++)
  {
    if (idBuffer[i].ID == ID)
    {
      idBuffer[i].totalLapsTime = totalTime;
      idBuffer[i].gateNumber = gate;
      idBuffer[i].isNew = true;
      break;
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
              break;
          }
      }
      break;
    }
  }
}


void fakeIDtrigger(int ms){
  uint8_t gate = 0;
    for (uint8_t i = 0; i < NUMBER_RACER; i++)
    {
        if (ms - idListLastMillis[i] > idListTimer[i]){
            idListLastMillis[i] = ms;
            idListTimer[i] = random(5000, 8000);

            uint8_t gateNumber_tmp = idGateNumber[i] + 1;
            gate = (gateNumber_tmp < 23) ? gateNumber_tmp : 20;
            idGateNumber[i] = gate;
            // Serial.println(i);
            bufferingID(idList[i], gate, ms);
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
       // look ID current position
       uint8_t currentPosition = 0;
       uint32_t currentTime = idBuffer[i].totalLapsTime;
       uint8_t currentGate = idBuffer[i].gateNumber;
       
       idBuffer[i].isNew = false;

       // get current position
       for (uint8_t j = 1; j < NUMBER_RACER + 1 ; j++)
       {
         if ( idBuffer[i].ID == idData[j].ID )
         {
            currentPosition = j;
            idData[currentPosition].updateTime(currentTime, currentGate);
            break;
         }
       }
       // check if ID lowering position/rank.
       for (uint8_t k = currentPosition; k > 1 ; k--)
       {
          if (idData[k].laps > idData[k - 1].laps)
          {
              for (uint8_t l = 0; l < 3; l++)
              {
                idData[k].positionChange[l] = true;
                idData[k - 1].positionChange[l] = true;
              }

              idData[0] = idData[k - 1]; // backup copy
              idData[k - 1] = idData[k];
              idData[k] = idData[0];
//              idData[0].laps = 0;
//              idData[0].isNew = false;
          }
          else
          {
//            idData[k].statPos = 0;
            break;
          }
       }
    }
  }
}


void loop() {
    fakeIDtrigger(millis());
    sortIDLoop();
    serialPrint(millis());
}


void serialPrint(uint32_t ms){
  static uint32_t lastMillis = 0;
  const uint32_t delayMillis = 1000;
  

  for (uint8_t i = 1; i < ARRAY_LENGTH(idData) ; i++){
    if (idData[i].positionChange[0] == true){
      Serial.print(i);
      Serial.print(" | ");
      Serial.print(idData[i].laps);
      Serial.println(" | full line change");
      idData[i].positionChange[0] = false;
      break; // don't flood!
    }
    if (idData[i].haveSerialUpdate()){
      uint8_t gateChange = idData[i].indexToChange;
      Serial.print(i);
      Serial.print(" | ");
      Serial.print(idData[i].currentCheckPoint[gateChange]);
      Serial.println(" | Gate update");
      break;
    }
  }




  // test if positionChange have changed!
  //    done a full data print!
  // test if for each player some Gate have been changed : class.haveSerialUpdate()
//  if (ms - lastMillis > delayMillis)
  // if (idChanged == true)
  // {
  //   lastMillis = ms;
  //   idChanged = false;
  //   for (uint8_t i = 1; i < NUMBER_RACER + 1; i++)
  //   {
  //       Serial.print(i);
  //       // Serial.print(" | lastPos: ");
  //       // Serial.print(idData[i].lastPos); 
  //       // Serial.print(" | rank: ");
  //       // Serial.print(idData[i].statPos); 
  //       // Serial.print(" | gate: ");
  //       // Serial.print(idData[i].currentGate); 
  //       // Serial.print(" | ID: ");
  //       // Serial.print(idData[i].ID, HEX);
  //       // Serial.print(" | laps: ");
  //       // Serial.print(idData[i].laps);       
  //       // Serial.print(" | best: ");
  //       // Serial.print(idData[i].bestLap);
  //       // Serial.print(" | mean: ");
  //       // Serial.print(idData[i].meanLap);
  //       // Serial.print(" | last: ");
  //       // Serial.print(idData[i].lastLap);
  //       // Serial.print(" | total: ");
  //       // Serial.println(idData[i].totalLap);
  //    }
  //    Serial.println("-------------------------");
  // }
}