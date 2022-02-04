function init() {
    ourURL = window.location.href;
    console.log("ourURL = "+ ourURL);
    chop = 0;
    if (ourURL.startsWith("http://")) {
        chop = 7;
    } else if (ourURL.startsWith("https://")) {
        chop = 8;
    } else {
        console.log("window.location.href is not an http URL");
        document.getElementById('potVal').innerHTML = "<font color=\"red\">!! NO HOST !!</font>";
    }

    if (chop != 0) {
        tmp = ourURL.slice(chop);
        if ((idx = tmp.indexOf("/")) != -1) {
            ourHost = tmp.slice(0, idx);
            console.log("ourHost = "+ ourHost);

            url = "ws://"+ ourHost +"/ws";
            console.log("url = "+ url);

            // Connect to WebSocket server
            wsConnect(url);
        } else {
            console.log("Could not determine hostname from window.location.href");
            document.getElementById('potVal').innerHTML = "<font color=\"red\">!! NO HOST !!</font>";
        }
    }
}

// Call this to connect to the WebSocket server
function wsConnect(url) {
    // Connect to WebSocket server
    websocket = new WebSocket(url);
    
    // Assign callbacks
    websocket.onopen = function(evt) { onOpen(evt) };
    websocket.onclose = function(evt) { onClose(evt) };
    websocket.onmessage = function(evt) { onMessage(evt) };
    websocket.onerror = function(evt) { onError(evt) };
}

// Called when a WebSocket connection is established with the server
function onOpen(evt) {
//    window.alert("Connected");
    // Log connection state
    console.log("Connected");
//        refreshElements(evt.data);
    // // Get the current state of ??
    // doSend("get??State");
}

// Called when the WebSocket connection is closed
function onClose(evt) {

    // Log disconnection state
    console.log("Disconnected");
    
    // Try to reconnect after a few seconds
    setTimeout(function() { wsConnect(url) }, 2000);
}

// Called when a message is received from the server
function onMessage(evt) {

    // Print out our received message
    console.log("Received: " + evt.data);
    
    obj = JSON.parse(evt.data);

    //var size = Object.keys(obj).length
    // no sanety check for player update... but cleaner!?!
    if ('data' in obj) {
        tmp_data =  obj.data;
        for (const [key, value] of Object.entries(tmp_data)) {
            if (/_class$/.test(`${key}`)){
                var color_class = ['white', 'rgb(66, 133, 244)', 'rgb(234, 67, 53)', 'rgb(251, 188, 5)', 'rgb(52, 168, 83)'];
                document.getElementsByClassName(`${key}`)[0].style.backgroundColor = color_class[`${value}`];
            } else if (/one_current/.test(`${key}`)){
                one_class_stopwatch.start("one_current", `${value}`);
            } else if (/two_current/.test(`${key}`)){
                two_class_stopwatch.start("two_current", `${value}`);
            } else if (/three_current/.test(`${key}`)){
                three_class_stopwatch.start("three_current", `${value}`);
            } else if (/four_current/.test(`${key}`)){
                four_class_stopwatch.start("four_current", `${value}`);
            } else {
                document.getElementById(`${key}`).innerHTML = "" + `${value}`;
            }
        }
    }

    if ('message' in obj) {
        document.getElementById('message').innerHTML = obj.message;
        snackBar();
    }

    if ('websockTimer' in obj) {
        document.getElementById('websockTimer').innerHTML = "" + obj.websockTimer;
    }

    if ('lapCounter' in obj) {
        document.getElementById('lapCounter').innerHTML = "" + obj.lapCounter;
    }

    if ('percentLap' in obj) {
        var tmpWidth = obj.lapCounter / document.getElementById("lapsSlider").value * 100;
        console.log("ERROR: " + tmpWidth);
        document.getElementById("percentLap").style.width = tmpWidth + "%";
        // document.getElementById('percentLap').value = obj.lapCounter;
        // document.getElementById('percentLap').innerHTML = obj.lapCounter;
    }    

    if ('websockConsole' in obj) {
        document.getElementById('websockConsole').innerHTML = "" + obj.websockConsole;
    }

    if ('race' in obj) {
            var x = document.getElementById("race");
            if (obj.race === 1) {
                x.innerHTML = "Stop";
                x.style.color = "red";
            } else {
                x.innerHTML = "Start";
                x.style.color = "green";
            }
    }

    if ('light' in obj) {
            var x = document.getElementById("light");
            if (obj.light === 1) {
                x.innerHTML = "Off";
                x.style.color = "blue";
//                x.style.bgcolor = "red";
            } else {
                x.innerHTML = "On";
                x.style.color = "black";
//                x.style.bgcolor = "grey";
            }
    }

    if ('connect' in obj) {
            var x = document.getElementById("connect");
            if (obj.connect === 1) {
                x.innerHTML = "Disconnect";
            } else {
                x.innerHTML = "Connect";
            }
    }

    if ('stopwatch' in obj) {
        // var x = document.getElementById("startstop_watch");
        if (obj.stopwatch === 1) {
            // x.innerHTML = "Stop";
            // x.style.color = "red";
            stopwatch.start("stopwatch");
        } else {
            // x.innerHTML = "Start";
            // x.style.color = "green";
            stopwatch.stop();
        }
    }

    if ('raceTime' in obj) {
        document.getElementById('debugtime').innerHTML = "" + obj.raceTime;
    }

    if ('setlaps' in obj) {
        document.getElementById("textSliderValue").innerHTML = "" + obj.setlaps;
        document.getElementById("lapsSlider").value = "" + obj.setlaps;
        // document.getElementById("percentLap").max = "" + obj.setlaps;
    }


    // Lock
    if ('startLock' in obj) {
        var x = document.getElementById("race");
        if (obj.startLock === 1) {
            x.disabled = true;
        } else {
            x.disabled = false;
        }
    }

    if ('lightLock' in obj) {
        var x = document.getElementById("light");
        if (obj.lightLock === 1) {
            x.disabled = true;
        } else {
            x.disabled = false;
        }
    }

    if ('connectLock' in obj) {
        var x = document.getElementById("connect");
        if (obj.connectLock === 1) {
            x.disabled = true;
        } else {
            x.disabled = false;
        }
    }

    if ('sliderLock' in obj) {
        var x = document.getElementById("lapsSlider");
        if (obj.sliderLock === 1) {
            x.disabled = true;
        } else {
            x.disabled = false;
        }
    }
}


function lockElement(what){
    var colors = {
        'Blue': function(){ alert('Light-Blue'); },
        'Red': function(){ alert('Deep-Red'); },
        'Green': function(){ alert('Deep-Green'); }
        }
    try {colors[what]();}
    catch(err) {colors['Green']();}//default behaviour
}

// Called when a WebSocket error occurs
function onError(evt) {
    console.log("ERROR: " + evt.data);
}

// Sends a message to the server (and prints it to the console)
function doSend(message) {
    console.log("Sending: " + message);
    websocket.send(message);
}

// UI functions callback
function refresh(clicked_id) {
    var x = document.getElementById(clicked_id);
    var button = {
        'Start': function(){ doSend("{ \"race\": 1}"); },
        'Stop': function(){ doSend("{ \"race\": 0}"); },
        'On': function(){ doSend("{ \"light\": 1}"); },
        'Off': function(){ doSend("{ \"light\":0}"); },
        'Connect': function(){ doSend("{ \"connect\": 1}"); },
        'Disconnect': function(){ doSend("{ \"connect\": 0}"); }
        'lapsSlider': function(){ doSend("{ \"setlaps\": "+ x.value +" }"); }
    }
    
    try {button[x]();}
    catch(err) {alert(x);}//default behaviour
}


function snackBar() {
    var x = document.getElementById("snackbar");
    x.className = "show";
    setTimeout(function(){ x.className = x.className.replace("show", ""); }, 5000);
  }

class Stopwatch {
    constructor(id, delay=151) { //Delay in ms
      this.state = "paused";
      this.delay = delay;
      this.display = document.getElementById("stopwatch");
      this.interval;
      this.currentTime = [0, 0, 0];
      this.startTime = new Date();
      this.totalTime;
    }
    
    formatTime() {
        if (this.currentTime[0] < 10){
            this.currentTime[0] = "0" + this.currentTime[0];
        }
        if (this.currentTime[1] < 10){
            this.currentTime[1] = "0" + this.currentTime[1];
        }
        if(this.currentTime[2] < 10){
            this.currentTime[2] = "00" + this.currentTime[2];
        }
        else if(this.currentTime[2] < 100){
            this.currentTime[2] = "0" + this.currentTime[2];
        }
      return this.currentTime[0] + ":" + this.currentTime[1] + "." + this.currentTime[2];
    }
    
    update(idtmp) {
      if (this.state=="running") {
        var end = new Date();
        var diff = end - this.startTime;
        diff = new Date(diff);

        this.currentTime[0] = diff.getMinutes();
        this.currentTime[1] = diff.getSeconds();
        this.currentTime[2] = diff.getMilliseconds();
        document.getElementById(idtmp).innerHTML = "" + this.formatTime();
        if (this.display > 0) {
            this.display.innerHTML = "" + this.formatTime();
        }
      }
    }
    
    start(idtmp, totalTime) {
        if (this.totalTime != totalTime){
            this.totalTime = totalTime;
            this.startTime = new Date();
  
            if (this.state=="paused") {
                this.state="running";
                if (!this.interval) {
                var t=this;
                this.interval = setInterval(function(){t.update(idtmp);}, this.delay);
                }
            }
        }
    }
    
    stop() {
      if (this.state=="running") {
        this.state="paused";
  
        if (this.interval) {
            clearInterval(this.interval);
            this.interval = null;
        }
      }
    }

}

stopwatch = new Stopwatch("stopwatch");
one_class_stopwatch = new Stopwatch("stopwatch");
two_class_stopwatch = new Stopwatch("stopwatch");
three_class_stopwatch = new Stopwatch("stopwatch");
four_class_stopwatch = new Stopwatch("stopwatch");
window.addEventListener("load", init, false);




#include<EEPROM.h>
 
// cell number1 
int cn1_starting_address = 10; // cell1 number starting address
int cn1_ending_address = 23; // ending address
 
// cell number2 
int cn2_starting_address = 30; // cell2 number starting address
int cn2_ending_address = 43;  // ending address

 
int eeprom_Memory_address = 0; 
int read_eepromDATA = 0; 
char serialDATA_to_write;
int write_memoryLED = 13; 
int end_memoryLED = 12; 
int eeprom_size = 1024; 
 
String number1 = ""; 
 
char data; 
 
void setup() {
pinMode(write_memoryLED,OUTPUT); 
pinMode(end_memoryLED, OUTPUT); 
Serial.begin(9600); 
Serial.println(); 
 
 
// access the previous stored numbers and save them in variables.
previous_numbers_saved(); 
 
     
 
}
 
void loop() {
 
if ( Serial.available() > 0 )
{
 
data = Serial.read(); 
 
  if( data == 'a' ) // command to save cell number 1
  {
    save_number1();
  }
  
  // commands for numbers erasing 
 
        if( data == 'l' ) // erase number 1
  {
 erase_number1();
 number1 = "";  
  }
 
 
      if( data == 'y' ) // command to erase all the numbers
  {
    erase_memory(); 
String number1 = ""; 
String number2 = "";
String number3 = "";
String number4 = "";
String number5 = "";
String number6 = "";
String number7 = "";
String number8 = "";
String number9 = "";
String number10 = "";
delay(1000);
  }
 
      if( data == 'z' ) // command to display all the saved numbers. 
  {
Serial.println("numbers saved are");
Serial.println("Number1: ");  
Serial.println(number1); 
  }
}
 
}
// ************************** Erase Memory Function *************************
void erase_memory()
{
 
  for(eeprom_Memory_address = 0; eeprom_Memory_address < eeprom_size; eeprom_Memory_address ++)
{
 EEPROM.write(eeprom_Memory_address, " ");  
}
Serial.println("Memory Erased!!!"); 
 
}
//********************************** Previously Stored Numbers**********************************
 
void previous_numbers_saved()
{
 
Serial.println(" All the previously saved numbers"); 
      for(eeprom_Memory_address = cn1_starting_address; eeprom_Memory_address < cn1_ending_address; eeprom_Memory_address ++)
    {
      read_eepromDATA = EEPROM.read(eeprom_Memory_address); 
      number1 = number1 + char(read_eepromDATA);
    }
 
}
 
void save_number1()
{
  number1 = ""; 
   Serial.println("enter number 1: ");
  for ( eeprom_Memory_address = cn1_starting_address; eeprom_Memory_address < cn1_ending_address;)
{
  if ( Serial.available())
  {
    serialDATA_to_write = Serial.read();
    Serial.write(serialDATA_to_write); 
    EEPROM.write(eeprom_Memory_address, serialDATA_to_write);
    eeprom_Memory_address++; 
    number1 = number1 + serialDATA_to_write; 
    digitalWrite(write_memoryLED, HIGH); 
    delay(50); 
    digitalWrite(write_memoryLED, LOW); 
  }
}
  Serial.println("number saved"); 
}
 
// erase individual numbers
 
// ************************** Erase Memory Function *************************
void erase_number1()
{
 
  for(eeprom_Memory_address = cn1_starting_address; eeprom_Memory_address <= cn1_ending_address; eeprom_Memory_address ++)
{
 EEPROM.write(eeprom_Memory_address, " ");  
}
Serial.println("Cell number1 Erased"); 
 
}

 




#include <EEPROM.h> 

int addr_ssid = 0;         // ssid index
int addr_password = 20;    // password index
String ssid = "wifi_ssid"; // wifi ssid
String password = "wifi_password_demo"; // and password

// Set to true to reset eeprom before to write something
#define RESET_EEPROM false

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  Serial.println("");
  
  if ( RESET_EEPROM ) {
    for (int i = 0; i < 512; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
    delay(500);
  }
 
  Serial.println("");
  Serial.print("Write WiFi SSID at address "); Serial.println(addr_ssid);
  Serial.print("");
  for (int i = 0; i < ssid.length(); ++i)
  {
    EEPROM.write(addr_ssid + i, ssid[i]);
    Serial.print(ssid[i]); Serial.print("");
  }

  Serial.println("");
  Serial.print("Write WiFi Password at address "); Serial.println(addr_password);
  Serial.print("");
  for (int j = 0; j < password.length(); j++)
  {
    EEPROM.write(addr_password + j, password[j]);
    Serial.print(password[j]); Serial.print("");
  }

  Serial.println("");
  if (EEPROM.commit()) {
    Serial.println("Data successfully committed");
  } else {
    Serial.println("ERROR! Data commit failed");
  }

  Serial.println("");
  Serial.println("Check writing");
  String ssid;
  for (int k = addr_ssid; k < addr_ssid + 20; ++k)
  {
    ssid += char(EEPROM.read(k));
  }
  Serial.print("SSID: ");
  Serial.println(ssid);

  String password;
  for (int l = addr_password; l < addr_password + 20; ++l)
  {
    password += char(EEPROM.read(l));
  }
  Serial.print("PASSWORD: ");
  Serial.println(password);
}

void loop() {

}



/*************************** sToi Function *************************************
** This function takes in a string and returns an int.  Arguments are a      **
** string to process, and the location in the string of the information you  **
** want (using substring notation)       yourIntNameHere = sToi(yourStringNameHere,13,16);                                    **
*******************************************************************************/

int sToi (String & a, int x, int y){
    int tempArraySize = y - x + 1;
    int outPut;
    char tempArray[tempArraySize];
    String tempString = a.substring(x,y);
    tempString.toCharArray(tempArray, sizeof(tempArray));
    outPut = atoi(tempArray);
    return outPut;
  }