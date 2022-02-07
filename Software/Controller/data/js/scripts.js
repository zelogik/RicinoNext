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
        //document.getElementById('potVal').innerHTML = "<font color=\"red\">!! NO HOST !!</font>";
    }

    // todo: make a ourHost try AND after try ricinoNext.local
    if (chop != 0) {
        tmp = ourURL.slice(chop);
        if ((idx = tmp.indexOf("/")) != -1) {
            // ourHost = tmp.slice(0, idx);
            // console.log("ourHost = "+ ourHost);

            websocketUrl = "ws://"+ "ricinoNext.local" +"/ws";
            console.log("url = "+ websocketUrl);

            // Connect to WebSocket server
            wsConnect(websocketUrl);
        } else {
            console.log("Could not determine hostname from window.location.href");
            document.getElementById('potVal').innerHTML = "<font color=\"red\">!! NO HOST !!</font>";
        }
    }
}

// Call this to connect to the WebSocket server
function wsConnect(websocketUrl) {
    // Connect to WebSocket server
    websocket = new WebSocket(websocketUrl);

    // Assign callbacks
    websocket.onopen = function(evt) { onOpen(evt) };
    websocket.onclose = function(evt) { onClose(evt) };
    websocket.onmessage = function(evt) { onMessage(evt) };
    websocket.onerror = function(evt) { onError(evt) };
}

// Called when a WebSocket connection is established with the server
function onOpen(evt) {
    // Log connection state
    console.log("Connected");
    // refreshElements(evt.data);
    // Get the current state of ??
    // doSend("get??State");
    // screen.orientation.lock()
}

// Called when the WebSocket connection is closed
function onClose(evt) {

    // Log disconnection state
    console.log("Disconnected");
    
    // Try to reconnect after a few seconds
    setTimeout(function() { wsConnect(websocketUrl) }, 2000);
}

// Called when a message is received from the server
function onMessage(evt) {
    obj = JSON.parse(evt.data);
    if ( !('live' in obj) || (DEBUG_LIVE) ){
        // Print out our received message
        console.log("Received: " + evt.data);
    }

    //var size = Object.keys(obj).length
    // no sanety check for player update... but cleaner!?!
    if ('data' in obj) {
        tmp_data =  obj.data;
        for (const [key, value] of Object.entries(tmp_data)) {
            if (/_class$/.test(`${key}`)){
                var color_class = ['white', 'rgb(66, 133, 244)', 'rgb(234, 67, 53)', 'rgb(251, 188, 5)', 'rgb(52, 168, 83)'];
                // todo: doesn't work right now
                //document.getElementsByClassName(`${key}`)[0].style.backgroundColor = color_class[`${value}`];
            } else if (/_current$/.test(`${key}`)){
                stopwatches[`${key}`].start(`${key}`, `${value}`);
            } else {
                document.getElementById(`${key}`).innerHTML = "" + `${value}`;
            }
        }
    }

    if ('conf' in obj) {
        //var names = Object.keys(jsonObj.products);
        // products = ["laptop", "cellphone", "tablets"];
        config_global = obj;

        if ('laps' in obj.conf) {
            var laps_value = document.getElementById("lapsSlider").value;

            if ( obj.conf.laps != laps_value) {
                document.getElementById("textSliderValue").innerHTML = "" + obj.conf.laps;
                document.getElementById("lapsSlider").value = "" + obj.conf.laps;
            }
        }

        if ('light' in obj.conf) {
            var light_obj = document.getElementById("light");
            // var light_state = ((document.getElementById("light").innerHTML) === "Off" ? 0 : 1 );

            // console.log("light" + light_obj)
            // if (obj.conf.light != light_state){
                if (obj.conf.light > 1) {
                    light_obj.innerHTML = "On";
                    light_obj.style.color = "black";
            //      x.style.bgcolor = "red";
                } else {
                    light_obj.innerHTML = "Off";
                    light_obj.style.color = "blue";
            //      x.style.bgcolor = "grey";
                }
            // }
        }
    }


    if ('race' in obj) { // Race is read only!
            var raceButton = document.getElementById("race");
            var sliderLock = document.getElementById("lapsSlider");

            if (obj.race.state === "WAIT") {
                sliderLock.disabled = false;
                raceButton.innerHTML = "Start";
                raceButton.style.color = "green";
            }
            else if (obj.race.state === "STOP") {
                stopStopWatches();
            }
            else {
                sliderLock.disabled = true;
                raceButton.innerHTML = "Stop";
                raceButton.style.color = "red";
            }

            if ('lap' in obj.race) {
                var tmpWidth = obj.race.lap / document.getElementById("lapsSlider").value* 100;
                // console.log("ERROR: " + tmpWidth);
                document.getElementById('percentLap').style.width = tmpWidth + "%";
                document.getElementById('percentLap').innerHTML = obj.race.lap;
                // document.getElementById('percentLap').value = obj.conf.laps;
                document.getElementById('lapCounter').innerHTML = "" + obj.race.lap;
            }

            if ('message' in obj.race) {
                snackBar(obj.race.message);
                // document.getElementById('websockConsole').innerHTML = "" + obj.race.message;
            }

            if ('time' in obj.race) {
                document.getElementById('websockTimer').innerHTML = "" + obj.race.time;
                // document.getElementById('debugtime').innerHTML = "" + obj.race.time;
            }
    }


    if ('live' in obj) { // Live is read only

        var x = document.getElementById("race");
        var id1 = obj.live.rank;

        createLine(id1);

        modifyValue( id1, "id", obj.live.id);
        //modifyValue( id1, "name", obj.live.name);
        modifyValue( id1, "lap", obj.live.lap);
        modifyValue( id1, "best", formatTime(obj.live.best, 0));
        modifyValue( id1, "last", formatTime(obj.live.last, 0));
        modifyValue( id1, "mean", formatTime(obj.live.mean, 0));
        modifyValue( id1, "total", formatTime(obj.live.total, 0));
        stopwatches[ id1 + "_current"].start(id1 + "_current", obj.live.total);
        updatePlayer( id1, obj.live.id );

        if (obj.live.lap == config_global.conf.laps) {
            console.log(obj.live.lap);
            stopwatches[id1 + "_current"].stop();
        }
    }


    if ('debug' in obj) {
        if ('time' in obj.debug) {
            document.getElementById('websockLoopTime').innerHTML = "" + obj.debug.time;
        }

        if ('message' in obj.debug) {
            document.getElementById('websockConsole').innerHTML = "" + obj.debug.message;
        }
    }
}

// Called when a WebSocket error occurs
function onError(evt) {
    console.log("ERROR: " + evt.data);
}

// Sends a message to the server (and prints it to the console)
function doSend(message) {
    console.log("Sending: " + message);
    try{
        websocket.send(message);
    }
    catch (err) {
        //socket error
    }
}

// UI functions callback
function raceToggle() {
    var x = document.getElementById("race");
    
    if (x.innerHTML === "Start") {
        var data = JSON.stringify({"conf": {"state": "1"}});
    } else {
        var data = JSON.stringify({"conf": {"state": "0"}});
    }
    doSend(data);
}

function lightToggle() {
    var x = document.getElementById("light");
    
    if (x.innerHTML === "On") {
        var data = JSON.stringify({"conf": {"light": "0"}});
    } else {
        var data = JSON.stringify({"conf": {"light": "255"}});
    }
    doSend(data);
}

function resetTrigger() {
            clearLines();
            var data = JSON.stringify({"conf": {"reset": "1"}});
            doSend(data);
}

function updateLaps(element) {
    var sliderValue = document.getElementById("lapsSlider").value;
    // console.log("Sending: " + sliderValue);
    var data = JSON.stringify({"conf": {"laps": sliderValue}});
    doSend(data);
}

function generateDiv(class_name, id1, string)
{
    var divd = document.createElement("div");
    divd.setAttribute('class', class_name);
    divd.setAttribute('id', id1);
    divd.innerHTML = string;
    return divd;
}

function updatePlayer(line_id, id1)
{
    var divd = document.getElementById(line_id + '_name');

    for (const [key, value] of Object.entries(config_global.conf.names))
    {
        //console.log(key);
        //console.log(value.id);
        //console.log(value.name);
        if (id1 === value.id)
        {
            divd.innerHTML = value.name;
            document.getElementById(line_id + "_class").style.backgroundColor = value.color;
        }
    }
}

function modifyValue(line_id, class1, string1)
{
    var divd = document.getElementById(line_id + '_' + class1);
    divd.innerHTML = string1;
}

function createLine(line_id)
{
    for (var i = 1; i <= line_id; i++)
    {
        var line_elem = document.getElementById(i + '_class');
        if (!line_elem)
        {
            generateLine(i);
        }
    }
}

function generateLine(line_id)
{
    var new_line = document.createElement("div");
    if (line_id % 2 === 0)
    {
       new_line.setAttribute('class', 'even_line');
    }
    else
    {
       new_line.setAttribute('class', 'odd_line');
    }
    new_line.setAttribute('id', line_id + '_class');
    new_line.innerHTML = "";

	new_line.appendChild(generateDiv('col-md-0', line_id + '_id', line_id));
	new_line.appendChild(generateDiv('col-md-1', line_id + '_lap', '-'));
	new_line.appendChild(generateDiv('col-md-1', line_id + '_name', 'Driver' + line_id));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_last', '-'));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_best', '-'));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_mean', '-'));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_total', '-'));
	new_line.appendChild(generateDiv('col-md-0', line_id + '_current', '0'));

    document.getElementById("stats").appendChild(new_line);
    
    stopwatches[line_id + "_current"] = new Stopwatch("stopwatch");
    //stopwatches[line_id + "_current"].start(line_id + '_current', 0);
}

function removeLine(line_id)
{
    var line_elem = document.getElementById(line_id + '_class');
	if (line_elem) {
	  document.getElementById("stats").removeChild(line_elem);
	  if (stopwatches[line_id + "_current"])
	      stopwatches[line_id + "_current"].stop();
	  delete stopwatches[line_id + "_current"];
    }
}

function clearLines()
{
    // todo: should get the line number from conf?
    for (var i = 1; i <= 4; i++)
    {
        removeLine(i);
    }
}

function formatTime(time, startTime)
{
    var currentTime = [ 0, 0, 0 ];
    var diff = time - startTime;
    diff = new Date(diff);

    currentTime[0] = diff.getMinutes();
    currentTime[1] = diff.getSeconds();
    currentTime[2] = diff.getMilliseconds();

    if (currentTime[0] < 10){
        currentTime[0] = "0" + currentTime[0];
    }
    if (currentTime[1] < 10){
        currentTime[1] = "0" + currentTime[1];
    }
    if(currentTime[2] < 10){
        currentTime[2] = "00" + currentTime[2];
    }
    else if(currentTime[2] < 100){
        currentTime[2] = "0" + currentTime[2];
    }

    return currentTime[0] + ":" + currentTime[1] + "." + currentTime[2];
}

function snackBar(message) {
    var x = document.getElementById("snackbar");
    x.innerHTML = message;
    x.className = "show";
    setTimeout(function(){ x.className = x.className.replace("show", ""); }, 5000);
    console.log(message);
  }

function stopStopWatches()
{
    // todo: need a trigger message from the server?
    Object.keys(stopwatches).forEach(function(key)
    {
        // console.log("key" + key); 

        // if (key == position ) {
            stopwatches[key].stop();
        // }
    });
    stopwatch.stop();
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
    
    update(idtmp) {
      if (this.state=="running") {
        var curTime = formatTime(new Date(), this.startTime);
        document.getElementById(idtmp).innerHTML = "" + curTime;
        if (this.display > 0) {
            this.display.innerHTML = "" + curTime;
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

var DEBUG_LIVE = false; //Set this to true if you want to log the live json events as well (spams a lot)
let stopwatches = {};
stopwatch = new Stopwatch("stopwatch");
let config_global = "";

window.addEventListener("load", init, false);
