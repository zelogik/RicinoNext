function init()
{
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
function wsConnect(websocketUrl)
{
    // Connect to WebSocket server
    websocket = new WebSocket(websocketUrl);

    // Assign callbacks
    websocket.onopen = function(evt) { onOpen(evt) };
    websocket.onclose = function(evt) { onClose(evt) };
    websocket.onmessage = function(evt) { onMessage(evt) };
    websocket.onerror = function(evt) { onError(evt) };
}

// Called when a WebSocket connection is established with the server
function onOpen(evt)
{
    // Log connection state
    console.log("Connected");
    // refreshElements(evt.data);
    // Get the current state of ??, From Cédric: The backend send conf: at connection, and race is send every 5 : 1 sec , minimal unknown state...
    // doSend("get??State");
    // screen.orientation.lock()
}

// Called when the WebSocket connection is closed
function onClose(evt)
{

    // Log disconnection state
    console.log("Disconnected");
    
    // Try to reconnect after a few seconds
    setTimeout(function() { wsConnect(websocketUrl) }, 2000);
}

// Called when a message is received from the server
function onMessage(evt)
{
    obj = JSON.parse(evt.data);
    if ( !('live' in obj) || (DEBUG_LIVE) ){
        // Print out our received message
        console.log("Received: " + evt.data);
    }

    // Conf JSON is receiver at every conf change or at connection(including max)
    if ('conf' in obj) {
        config_global = obj;

        if ( maxValueSet == false)
        {
            maxValueSet = true;
            laps_maximum = obj.conf.laps_m;
            time_maximum = obj.conf.time_m;

            document.getElementById('conditionTotal').innerHTML = "" + laps_maximum;
            document.getElementById("conditionSlider").max = laps_maximum;
            document.getElementById("gatesSlider").max = obj.conf.gates_m;
            document.getElementById("playersSlider").max = obj.conf.players_m;
        }

        if ('style' in obj.conf) {
            var val = obj.conf.style;
            // var oldVal = document.getElementById('conditionText').innerHTML;
    
            if ( val == false ) // Laps Mode
            {
                document.getElementById('conditionText').innerHTML = "Laps / Total: ";
                document.getElementById("conditionSlider").max = laps_maximum;
                document.getElementById('style').selectedIndex = val;
            }
            else if ( val == true ) // time Mode
            {
                document.getElementById('conditionText').innerHTML = "Time / Total: ";
                document.getElementById("conditionSlider").max = time_maximum;
                document.getElementById('style').selectedIndex = val;
            }
        }

        // todo: Make laps, players, gates, time loopable
        // todo: Fix the laps/time change...
        // for (const [key, value] of Object.entries(tmp_data))
        // document.getElementById(`${key}`).innerHTML = "" + `${value}`;
        if ('laps' in obj.conf) {
            var laps_value = document.getElementById("conditionTotal").value;

            document.getElementById("conditionTotal").innerHTML = "" + obj.conf.laps;
            if ( obj.conf.laps != laps_value) {
                document.getElementById("conditionSlider").value = "" + obj.conf.laps;
            }
        }

        if ('players' in obj.conf) {
            var players_value = document.getElementById("players").innerHTML;

            document.getElementById("players").innerHTML = "" + obj.conf.players;
            if ( obj.conf.players != players_value) {
                document.getElementById("playersSlider").value = "" + obj.conf.players;
            }
        }

        if ('gates' in obj.conf) {
            var gates_value = document.getElementById("gates").innerHTML;

            document.getElementById("gates").innerHTML = "" + obj.conf.gates;
            if ( obj.conf.gates != gates_value) {
                document.getElementById("gatesSlider").value = "" + obj.conf.gates;
            }
        }

        if ('light' in obj.conf) {
            var light_obj = document.getElementById("light");

            if (obj.conf.light > 1) {
                light_obj.innerHTML = "On";
                light_obj.style.color = "black";  //      x.style.bgcolor = "red";
            } else {
                light_obj.innerHTML = "Off";
                light_obj.style.color = "blue";   //      x.style.bgcolor = "grey";
            }
        }

        if ('solo' in obj.conf) {
            var solo_value = document.getElementById("solo").checked;

            if ( obj.conf.solo != solo_value) {
                document.getElementById("solo").checked = obj.conf.solo;
            }
        }

        if ('reset') {
            if (obj.conf.reset){
                clearLines();
            }
        }
    }


    // Race JSON is receiver every X sec depend of the state, and at state Change
    if ('race' in obj) { // Race JSON is read only!
        var raceButton = document.getElementById("race");
        var sliderLock = document.getElementById("conditionSlider");

        if (obj.race.state === "WAIT") {
            sliderLock.disabled = false;
            raceButton.innerHTML = "Start";
            raceButton.style.color = "green";
            stopStopWatches(); // todo: backend need to send a STOP went client push stop
        }
        else if (obj.race.state === "STOP") {
            stopStopWatches();
        }
        else {
            sliderLock.disabled = true;
            raceButton.innerHTML = "Stop";
            raceButton.style.color = "red";
        }

        if ('lap' in obj.race || 'time' in obj.race) {
            if (config_global.conf.style == 0)
            {
                var conditionLapTime = obj.race.lap;
            }
            else
            {
                var conditionLapTime = obj.race.time;
            }

            var tmpWidth = conditionLapTime / document.getElementById("conditionSlider").value * 100;
            document.getElementById('percentLap').style.width = tmpWidth + "%";
            document.getElementById('percentLap').innerHTML = conditionLapTime;
            //document.getElementById('conditionCounter').innerHTML = "" + conditionLapTime; //I don't think this is working/needed
            // console.log("ERROR: " + tmpWidth);

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


    // live JSON is received at every player change, only one player at a time
    if ('live' in obj) { // Live is read only | config_global.conf

        if ( config_global.conf.solo == false)
        {
            var x = document.getElementById("race"); // not used anymore ?
            var id_rank = obj.live.rank;

            createLine(id_rank);
        }
        else
        {
            if (obj.live.lap != last_solo_line)
            {
                try
                {
                    stopwatches[(last_solo_line) + "_current"].stop();
                }
                catch(err) {}
                last_solo_line = obj.live.lap;
                generateLine(last_solo_line, true);
            }
            var id_rank = last_solo_line;
        }

        modifyValue( id_rank, "id", obj.live.id);
        //modifyValue( id1, "name", obj.live.name);
        modifyValue( id_rank, "lap", obj.live.lap);
        modifyValue( id_rank, "best", formatTime(obj.live.best, 0));
        modifyValue( id_rank, "last", formatTime(obj.live.last, 0));
        modifyValue( id_rank, "mean", formatTime(obj.live.mean, 0));
        modifyValue( id_rank, "total", formatTime(obj.live.total, 0));
        stopwatches[ id_rank + "_current"].start(id_rank + "_current", obj.live.total);
        updatePlayer( id_rank, obj.live.id );

        if (obj.live.lap == config_global.conf.laps) {
            console.log(obj.live.lap);
            stopwatches[id_rank + "_current"].stop();
        }
    }


    // send only debug "messages/things", not really useful for frontend
    if ('debug' in obj) {
        if ('time' in obj.debug) {
            document.getElementById('websockLoopTime').innerHTML = "" + obj.debug.time;
        }

        if ('message' in obj.debug) {
            document.getElementById('websockConsole').innerHTML = "" + obj.debug.message;
        }

        if ('memory' in obj.debug) {
            document.getElementById('websockFreeMemory').innerHTML = "" + obj.debug.memory;
        }
        
        if ('color' in obj.debug) {
            document.getElementById('websockRandomColor').innerHTML = "" + obj.debug.color;
        }
    }
}

// Called when a WebSocket error occurs
function onError(evt)
{
    console.log("ERROR: " + evt.data);
}

// Sends a message to the server (and prints it to the console)
function doSend(message)
{
    console.log("Sending: " + message);
    try{
        websocket.send(message);
    }
    catch (err) {
        //socket error
    }
}

// UI functions callback
// todo: make raceToggle/lightToggle one single function.
function raceToggle()
{
    var x = document.getElementById("race");
    
    if (x.innerHTML === "Start") {
        var data = JSON.stringify({"conf": {"state": "1"}});
    } else {
        var data = JSON.stringify({"conf": {"state": "0"}});
    }
    doSend(data);
}

function lightToggle()
{
    var x = document.getElementById("light").checked;
    
    if (x == true) {
        var data = JSON.stringify({"conf": {"light": "0"}});
    } else {
        var data = JSON.stringify({"conf": {"light": "255"}});
    }
    doSend(data);
}

function soloBox()
{
    var x = document.getElementById("solo").checked;

    if (x) {
        var data = JSON.stringify({"conf": {"solo": "1"}});
    } else {
        var data = JSON.stringify({"conf": {"solo": "0"}});
    }
    doSend(data);
}

function resetTrigger()
{
    var data = JSON.stringify({"conf": {"reset": "1"}});
    doSend(data);
}

function raceStyleChange() {
    var x = document.getElementById("style").value // .toUpperCase();
    var data = JSON.stringify({"conf": {"style": x}});
    doSend(data);
  }

// todo: factorize three below sliders functions
function updateCondition(element)
{
    var sliderValue = document.getElementById("conditionSlider").value;
    var data = JSON.stringify({"conf": {"laps": sliderValue}});
    // todo: send only new value every x sec, avoid flooding/DDOS
    doSend(data);
}

function updatePlayers(element)
{
    var sliderValue = document.getElementById("playersSlider").value;
    var data = JSON.stringify({"conf": {"players": sliderValue}});
    // todo: send only new value every x sec, avoid flooding
    doSend(data);
}

function updateGates(element)
{
    var sliderValue = document.getElementById("gatesSlider").value;
    var data = JSON.stringify({"conf": {"gates": sliderValue}});
    // todo: send only new value every x sec, avoid flooding
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
            generateLine(i, false);
        }
    }
}

function generateLine(line_id, insert_before)
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
	new_line.appendChild(generateDiv('col-md-1', line_id + '_name', 'Driver '+ line_id));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_last', '-'));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_best', '-'));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_mean', '-'));
	new_line.appendChild(generateDiv('col-md-2', line_id + '_total', '-'));
	new_line.appendChild(generateDiv('col-md-0', line_id + '_current', '0'));

    if (insert_before)
    {
      document.getElementById("stats").insertBefore(new_line, document.getElementById("stats").children[1]);
    } else
    {
      document.getElementById("stats").appendChild(new_line);
    }
    
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
    // todo: except if sliderValue have changed...
    // take {conf:players_m} ?  
    for (var i = 1; i <= config_global.conf.players; i++)
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

class Stopwatch
{
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

let last_solo_line = -1;

var DEBUG_LIVE = true; //Set this to true if you want to log the live json events as well (spams a lot)
let stopwatches = {};
stopwatch = new Stopwatch("stopwatch");
let config_global = "";
let config_global_connection = "";
let maxValueSet = false; // at connection frontend get max player/gate/time/laps value
var laps_maximum;
var time_maximum;

window.addEventListener("load", init, false);
