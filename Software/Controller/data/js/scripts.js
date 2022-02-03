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
