"""RicinoNext race core: state machine, lap/sector timing, gate packets, JSON protocol.

Platform independent: runs unchanged on CPython (PC simulator) and MicroPython (ESP32).
Laid out like Software/Controller/RicinoNext-controller.ino (same names, same structs, same
loop()) to keep a later C++ port easy. Deliberate fixes vs the firmware are tagged "FIX:".

The platform plugs in with setup():
  bus        .request(address, n) -> bytes   (Wire.requestFrom)   / .write(address, data) (Wire.write)
  millis()   monotonic milliseconds
  elapsed_us() microseconds since its previous call (debug loop time)
  mem_free() free memory as a string (debug)
  broadcast(text) / send_to(client, text)    WebSocket text frames
  load_settings() -> dict / save_settings(dict)   optional persistence of the race settings (EEPROM todo)
  load_records() -> dict / save_records(dict)     optional persistence of the arcade records (top 10)

Keep this file to the CPython / MicroPython common subset: no walrus, no os.path,
no time.monotonic, no bytes.ljust, % formatting instead of f-strings.
"""
import json, random, struct
from ricino.records import Records

VERSION = "1.0.0"

# ----------------------------------------------------------------------------
#  Hardware definitions
# ----------------------------------------------------------------------------
NUMBER_GATES_MAX = 3
NUMBER_RACER_MAX = 8
CARS_MAX = 50           # the garage: every car with its IR code
VERBOSE = True          # one log line per pass; the ESP32 turns it off (its console UART is slow)
PACKET_SIZE = 13
address_all_gates = [21, 22, 23, 24, 25, 26, 27, 28]  # order: 1st gate (finish line) -> ... -> last gate

SPEED = 1.0         # clock multiplier, simulation only (millis() already runs SPEED times faster)
loop_hooks = []     # extra callables run at each loop() (simulated cars)
platform_stats = None   # optional callable -> dict (RAM, flash, stack, CPU...) added to the debug message

_bus = None
_millis = None
_elapsed_us = None
_mem_free = None
_broadcast = None
_send_to = None
_save_settings = None
_save_records = None
records = Records()
_epoch_offset = None    # wall clock (s) - millis()/1000, given by a browser: the ESP32 has no clock


def setup(bus, millis, elapsed_us, mem_free, broadcast, send_to, speed=1.0, load_settings=None, save_settings=None,
          load_records=None, save_records=None):
    global _bus, _millis, _elapsed_us, _mem_free, _broadcast, _send_to, SPEED, _save_settings, _saved, _save_records
    _bus, _millis, _elapsed_us, _mem_free, _broadcast, _send_to = bus, millis, elapsed_us, mem_free, broadcast, send_to
    SPEED = speed
    _save_settings = save_settings
    _save_records = save_records
    if load_settings:
        try:
            apply_settings(load_settings())
        except Exception as e:          # missing / corrupted file: keep the defaults
            print("settings not loaded:", e)
    if load_records:
        try:
            records.load(load_records())
        except Exception as e:
            print("records not loaded:", e)
    seed_garage()
    _saved = settings()


def millis():
    return _millis()


def dumps(obj):
    try:
        return json.dumps(obj, separators=(",", ":"))
    except TypeError:                   # older MicroPython: no separators argument
        return json.dumps(obj)


# ----------------------------------------------------------------------------
#  Race style / state
# ----------------------------------------------------------------------------
LAPS, TIME, FREE = 0, 1, 2      # FREE: practice, no end condition (new)
STANDING, FLYING = 0, 1         # start: everybody's clock at the green flag / own clock at the first crossing (new)
RESET, WAIT, START, WARMUP, RACE, FINISH, STOP = range(7)
race_state_char = ["RESET", "WAIT", "START", "WARMUP", "RACE", "FINISH", "STOP"]


# ----------------------------------------------------------------------------
#  UI Config struct: changed by the client UI, sent at connection and on change
# ----------------------------------------------------------------------------
class UIConfig:
    def __init__(self):
        self.laps_condition = 5
        self.time_condition = 60 * 1000          # ms
        self.players = 4
        self.gates = 3
        self.state = False
        self.light = 0
        self.reset = False
        self.style = LAPS
        self.solo = False
        self.warmup = 10                         # start countdown in seconds (new)
        self.warmup_min, self.warmup_max = 5, 30
        self.start = FLYING
        self.read_id = False                     # tag reading mode (firmware todo "read_ID"), outside races
        self.min_pass_ms = 3000                  # a pass counts after this long without detection at the gate
        self.tolerance = 10                      # a skipped gate is tolerated if no gate error in the last N passes
        # maxima given to the frontend
        self.laps_max = 100
        self.time_max = 2 * 60 * 60 * 1000       # ms (sent in minutes as time_m)
        self.players_max = NUMBER_RACER_MAX
        self.gates_max = NUMBER_GATES_MAX
        # names[]: {id, name[16], color[8]}
        # colors: categorical palette, fixed order, checked for color-blind separation
        defaults = [(1234, "Player 1", "#2a78d6"), (1111, "Player 2", "#eb6834"),
                    (1337, "Player 3", "#1baf7a"), (2468, "Player 4", "#eda100"),
                    (6789, "Player 5", "#e87ba4"), (8080, "Player 6", "#008300"),
                    (7878, "Player 7", "#6250d6"), (8989, "Player 8", "#e34948")]
        self.names = [dict(id=i, name=n, color=c) for i, n, c in defaults]
        # garage: [{id: IR code, name}]; a driver (names[i]) picks a car, so names[i].id is that car's code
        self.cars = []


ui_config = UIConfig()


# ----------------------------------------------------------------------------
#  Main data struct: one per racer, id_data[1..players] is the ranking
# ----------------------------------------------------------------------------
def gate_index(gate):
    for i in range(ui_config.gates):
        if gate == address_all_gates[i]:
            return i
    return 0  # FIX: firmware returned an uninitialized index for unknown gates


class IDDataSorted:
    def __init__(self):
        self.ID = 0
        self.laps = 0
        self.best_lap_time = self.mean_lap_time = self.last_lap_time = self.last_total_time = 0
        self.sum_lap_time = 0
        self.last_total_check_point = [0] * NUMBER_GATES_MAX
        self.last_check_point = [0] * NUMBER_GATES_MAX
        self.best_check_point = [0] * NUMBER_GATES_MAX
        self.mean_check_point = [0] * NUMBER_GATES_MAX
        self.sum_check_point = [0] * NUMBER_GATES_MAX
        self.count_check_point = [0] * NUMBER_GATES_MAX
        self.have_update = False       # firmware: haveUpdate[protocol][gate]
        self.position_change = False   # firmware: positionChange[protocol]
        self.have_init_start = False
        self.start_time = 0            # clock origin: green flag (STANDING) or own first crossing (FLYING)
        self.current_gate = 0
        self.hit = self.strength = 0
        self.history = []              # every lap time of this race, sent to (re)connecting clients
        self.last_seen = [0] * NUMBER_GATES_MAX   # last detection per gate (debounce)
        self.next_gate = 0             # index of the gate expected next
        self.lap_valid = True          # False: a gate was skipped (not tolerated) in this lap
        self.recent = []               # gate error (True) or not, for the last passes
        self.missed = 0                # tolerated skipped gates
        self.cuts = 0                  # laps not counted

    def update_time(self, time_ms, gate):
        """One detection of this car at a gate (decoder time, ms). Rules:
          - debounce: it counts only after ui_config.min_pass_ms without ANY detection of this car at this
            gate (a car stopped on the line, or a double detection, does not count laps);
          - order: gates must be passed in order (0, 1, ..., n-1, 0); the timing starts at the first
            finish-line crossing. A skipped gate is tolerated when the car made no gate error in its last
            ui_config.tolerance passes; otherwise the lap in progress is not counted (cut / missed gate).
        """
        idx = gate_index(gate)
        seen = self.last_seen[idx]
        if time_ms > seen:
            self.last_seen[idx] = time_ms
        if seen and time_ms - seen < ui_config.min_pass_ms:
            return                                  # still in the gate, double detection, or older packet
        n = ui_config.gates
        if not self.have_init_start:
            if idx != 0:
                return                              # timing starts at the finish line
            # First crossing only arms the timing, no lap counted.
            #  STANDING: clocks run from the green flag, lap 1 = green flag -> 2nd crossing (firmware behavior)
            #  FLYING  : the driver's clock starts now, lap 1 = 1st -> 2nd crossing
            self.have_init_start = True
            self.start_time = time_ms if ui_config.start == FLYING else 0
            self.last_total_time = self.start_time
            self.last_total_check_point[idx] = time_ms
            self.next_gate = 1 % n
            self.current_gate = gate
            return
        # gate order
        error = idx != self.next_gate
        if error:
            w = ui_config.tolerance
            if w > 0 and True not in self.recent[-w:]:
                self.missed += 1
                event(self, "miss", self.next_gate)  # tolerated: probably a missed detection
            else:
                if self.lap_valid:
                    event(self, "cut", self.next_gate)
                self.lap_valid = False
        self.recent.append(error)
        if len(self.recent) > 50:
            self.recent.pop(0)
        self.next_gate = (idx + 1) % n
        self.current_gate = gate
        if idx == 0:                                # finish line: end of the lap in progress
            if self.lap_valid:
                self.laps += 1
                self.last_lap_time = time_ms - self.last_total_time
                self.sum_lap_time += self.last_lap_time
                self.mean_lap_time = self.sum_lap_time // self.laps   # FIX: firmware used total/laps
                if self.best_lap_time == 0 or self.last_lap_time < self.best_lap_time:
                    self.best_lap_time = self.last_lap_time
                if len(self.history) < HISTORY_MAX:
                    self.history.append(self.last_lap_time)
            else:
                self.cuts += 1                      # lap not counted; the next one starts here
            self.last_total_time = time_ms
            self.lap_valid = True
        # checkpoint (sector ending at this gate), only from the gate just before
        prev = n - 1 if idx == 0 else idx - 1
        if not error and self.last_total_check_point[prev]:
            cp = time_ms - self.last_total_check_point[prev]
            self.last_check_point[idx] = cp
            self.sum_check_point[idx] += cp
            self.count_check_point[idx] += 1
            self.mean_check_point[idx] = self.sum_check_point[idx] // self.count_check_point[idx]
            if self.best_check_point[idx] == 0 or cp < self.best_check_point[idx]:
                self.best_check_point[idx] = cp
        self.last_total_check_point[idx] = time_ms
        self.have_update = True


def event(d, kind, gate_idx):
    """Gate error of a car: "miss" (tolerated skipped gate) or "cut" (lap in progress not counted)."""
    print("%s: id %d gate %d (lap %d)" % (kind, d.ID, gate_idx + 1, d.laps + 1))
    text_all({"event": dict(type=kind, id=d.ID, gate=gate_idx + 1, lap=d.laps + 1)})


HISTORY_MAX = 500      # laps kept per driver (long solo practice sessions)

# [0] is the swap slot in the firmware; kept so ranks stay 1-based
id_data = [IDDataSorted() for _ in range(NUMBER_RACER_MAX + 1)]


# ----------------------------------------------------------------------------
#  Buffer struct: gate -> sort_id_loop()
# ----------------------------------------------------------------------------
class IDBuffer:
    def __init__(self):
        self.ID = 0
        self.gate_number = 0
        self.total_laps_time = 0
        self.is_new = False
        self.hit = self.strength = 0

    def fill(self, total_time, gate, hit, strength):
        self.total_laps_time, self.gate_number, self.hit, self.strength, self.is_new = total_time, gate, hit, strength, True


id_buffer = [IDBuffer() for _ in range(NUMBER_RACER_MAX)]


def buffering_id(ID, gate, total_time, hit, strength):
    """Called for each triggered gate (bufferingID)."""
    if not ui_config.solo:
        for b in id_buffer[: ui_config.players]:
            if b.ID == ID:
                b.fill(total_time, gate, hit, strength)
                break
            if b.ID == 0:                       # new emitter: register it
                b.ID = ID
                b.fill(total_time, gate, hit, strength)
                for d in id_data[1: ui_config.players + 1]:
                    if d.ID == 0:
                        d.ID = ID
                        break
                break
    else:                                       # solo: only the first ID seen
        if id_buffer[0].ID == 0:
            id_buffer[0].ID = ID
            id_data[1].ID = ID
        if id_buffer[0].ID == ID:
            id_buffer[0].fill(total_time, gate, hit, strength)


# ----------------------------------------------------------------------------
#  Gates info (0x82 / 0x83)
# ----------------------------------------------------------------------------
class GateData:
    def __init__(self):
        self.address = 0
        self.current_time = 0
        self.receiver_state = 0
        self.delta_offset_gate = 0
        self.bytes_in_buffer = 0
        self.loop_time = 0
        # receiver firmware 2+: error counters, queue length, version, flags (bit0 decoder lost, bit1 link lost)
        self.err_decoder = self.err_queue = self.err_link = self.queued = self.version = self.flags = 0


gate_data = [GateData() for _ in range(NUMBER_GATES_MAX)]


# ----------------------------------------------------------------------------
#  Race class: the state machine
# ----------------------------------------------------------------------------
START_CMD = bytes([0x8A])
STOP_CMD = bytes([0x8F])


def set_command(address, command):
    """Wire.beginTransmission / write / endTransmission."""
    _bus.write(address, command)


def command_all(command):
    """Start / stop every gate: one broadcast frame when the bus has it (RS485: gates start together)."""
    if getattr(_bus, "broadcast", False):
        set_command(0, command)
    else:
        for g in address_all_gates[: ui_config.gates]:   # FIX: firmware only told gate 21
            set_command(g, command)


# ----------------------------------------------------------------------------
#  Tag reading mode: finish-line gate listening outside a race, every pass sent as {"tag": ...}
#  (the receivers only decode IR once started by 0x8A)
# ----------------------------------------------------------------------------
READ_TIMEOUT = 30 * 1000        # stops after 30 s without a pass
_read_millis = 0


def start_read():
    global _read_millis
    if raceState != WAIT:
        return
    ui_config.read_id = True
    _read_millis = millis()
    set_command(address_all_gates[0], START_CMD)
    print("tag reading: on")


def stop_read(notify=False):
    if not ui_config.read_id:
        return
    ui_config.read_id = False
    set_command(address_all_gates[0], STOP_CMD)
    print("tag reading: off")
    if notify:
        conf_to_json()


def read_loop():
    if ui_config.read_id and millis() - _read_millis > READ_TIMEOUT * SPEED:
        stop_read(notify=True)


class Race:
    # FIX/todo of the firmware: "terminate after 20s OR all players have finished"
    finish_race_delay = 20 * 1000
    time_finish_warning = 10 * 1000            # time mode: warning before the end

    def __init__(self):
        self.biggest_lap = 0
        self.start_time = 0
        self.delay_warmup_timer = 0
        self.finish_race_millis = 0
        self.message = None
        self.warned = False
        self.finish_lap_limit = 0

    def init(self):
        global id_data, id_buffer
        self.biggest_lap = 0
        self.finish_race_millis = 0
        self.warned = False
        self.finish_lap_limit = 0
        ui_config.reset = False
        id_data = [IDDataSorted() for _ in range(NUMBER_RACER_MAX + 1)]
        id_buffer = [IDBuffer() for _ in range(NUMBER_RACER_MAX)]

    def set_message(self, msg):
        self.message = msg
        print("race:", msg)

    def get_message(self):
        msg, self.message = self.message, None
        return msg

    def is_id_running(self, d):
        # FIX: the lap limit only applies to the LAPS style
        if raceState == FINISH and ui_config.style == TIME:
            return d.laps < self.finish_lap_limit
        return not (ui_config.style == LAPS and d.laps >= ui_config.laps_condition)

    def sort_id_loop(self):
        # called ~200 times/s: index loop, no list slicing (each slice is an allocation for the GC)
        for i in range(ui_config.players):
            b = id_buffer[i]
            if not b.is_new:
                continue
            b.is_new = False
            pos = 0
            for j in range(1, ui_config.players + 1):
                if id_data[j].ID == b.ID:
                    pos = j
                    break
            if pos == 0:
                continue
            d = id_data[pos]
            if raceState in (RACE, FINISH) and self.is_id_running(d):
                d.hit, d.strength = b.hit, b.strength
                d.update_time(b.total_laps_time, b.gate_number)
                self.biggest_lap = max(self.biggest_lap, d.laps)
            # move up while having more laps than the one ahead
            k = pos
            while k > 1 and id_data[k].laps > id_data[k - 1].laps:
                id_data[k].position_change = id_data[k - 1].position_change = True
                id_data[k - 1], id_data[k] = id_data[k], id_data[k - 1]
                k -= 1

    def all_finished(self):
        running = [d for d in id_data[1: ui_config.players + 1] if d.ID]
        if not running:
            return False
        for d in running:
            if self.is_id_running(d):
                return False
        return True

    def check_final(self):
        global raceState
        if ui_config.style == FREE:
            return
        if ui_config.style == LAPS:
            if self.biggest_lap == ui_config.laps_condition - 1 and not self.warned and ui_config.laps_condition > 1:
                self.warned = True
                self.set_message("Final Lap")
            if self.biggest_lap >= ui_config.laps_condition:
                self.finish_race_millis = millis()
                raceState = FINISH
                self.set_message("Hurry UP!")
        else:
            remaining = ui_config.time_condition - (millis() - self.start_time)
            if remaining <= self.time_finish_warning and not self.warned:
                self.warned = True
                self.set_message("%d seconds remaining" % (self.time_finish_warning // 1000))
            if remaining <= 0:
                # time is over: everybody finishes the lap in progress (leader lap + 1 max)
                self.finish_lap_limit = self.biggest_lap + 1
                self.finish_race_millis = millis()
                raceState = FINISH
                self.set_message("Time! Last lap")

    def loop(self):
        global raceState
        self.sort_id_loop()
        if raceState == RESET:
            self.init()
            raceState = WAIT
        elif raceState == WAIT:
            pass
        elif raceState == START:
            stop_read()
            self.init()
            self.delay_warmup_timer = millis()
            raceState = WARMUP
            self.set_message("Départ dans %d s" % ui_config.warmup)
        elif raceState == WARMUP:
            if millis() - self.delay_warmup_timer >= ui_config.warmup * 1000:
                self.start_time = millis()
                raceState = RACE
                command_all(START_CMD)
                self.set_message("GO !")
        elif raceState == RACE:
            self.check_final()
        elif raceState == FINISH:
            if millis() - self.finish_race_millis > self.finish_race_delay or self.all_finished():
                raceState = STOP
                self.set_message("Finished")
        elif raceState == STOP:
            raceState = WAIT
            command_all(STOP_CMD)
            record_race()

    def get_current_time(self):
        return millis() - self.start_time if raceState in (RACE, FINISH) else 0


raceState = RESET
race = Race()


# ----------------------------------------------------------------------------
#  Gate polling and packet decoding
# - Pong: 0x82 | addr | crc | state | delta offset | serial buffer | loop time
# - ID:   0x84 | addr | crc | ID 4 bytes LE | TIME 4 bytes LE | strength | hit
# - Gate: 0x83 | addr | crc | TIME 4 bytes LE
# ----------------------------------------------------------------------------
def crc8(data):
    """Dallas/Maxim CRC-8 (reflected, 0x8C), the CRC8() of the receiver and original controller firmwares."""
    crc = 0
    for b in data:
        for _ in range(8):
            mix = (crc ^ b) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8C
            b >>= 1
    return crc


_gate_request_timer = 0
_gate_counter = 0
paused_until = 0        # diagnostics: no polling before this millis() (bus listening)


def request_gate():
    global _gate_request_timer, _gate_counter
    if millis() - _gate_request_timer < 20 * SPEED or millis() < paused_until:
        return
    _gate_request_timer = millis()
    if _gate_counter >= ui_config.gates:
        _gate_counter = 0
    packet = _bus.request(address_all_gates[_gate_counter], PACKET_SIZE)
    if packet and len(packet) >= 7:   # FIX: firmware parsed a stale buffer when nothing was received
        kind = packet[0]
        if kind == 0x83:
            process_gate_data(packet)
        elif kind == 0x84 and len(packet) >= PACKET_SIZE:
            process_id_data(packet)
        elif kind == 0x82:
            process_receiver_data(packet)
    _gate_counter = (_gate_counter + 1) % ui_config.gates


def _gate_slot(address):
    for g in gate_data:
        if g.address == address or g.address == 0:
            g.address = address
            return g
    return None


def process_gate_data(p):
    g = _gate_slot(p[1])
    if g:
        g.current_time = struct.unpack_from("<I", p, 3)[0]


def process_id_data(p):
    global _read_millis
    if crc8(p[3:13]) != p[2]:
        print("bad crc from gate", p[1])
        return
    ID, ms = struct.unpack_from("<II", p, 3)
    if ui_config.read_id:
        _read_millis = millis()
        text_all({"tag": dict(id=ID, gate=p[1], hit=p[11], strength=p[12])})
        return
    buffering_id(ID, p[1], ms, p[11], p[12])


def process_receiver_data(p):
    g = _gate_slot(p[1])
    if g:
        g.receiver_state, g.delta_offset_gate, g.bytes_in_buffer, g.loop_time = p[3], p[4], p[5], p[6]
        if len(p) >= PACKET_SIZE:
            g.err_decoder, g.err_queue, g.err_link, g.queued, g.version, g.flags = p[7], p[8], p[9], p[10], p[11], p[12]


# ----------------------------------------------------------------------------
#  Web stuff: struct -> JSON
# ----------------------------------------------------------------------------
def text_all(obj):
    if _broadcast:
        _broadcast(dumps(obj))


_cars_sent = None


def conf_to_json(client=None):
    c = ui_config
    conf = dict(laps=c.laps_condition, time=c.time_condition, players=c.players, gates=c.gates,
                light=c.light, reset=c.reset, style=c.style, solo=c.solo, warmup=c.warmup, start=c.start, read=1 if c.read_id else 0,
                debounce=c.min_pass_ms // 1000, tolerance=c.tolerance,
                state=1 if START <= raceState <= RACE else 0,
                names=c.names[: c.players])
    doc = {"conf": conf}
    global _cars_sent
    if client is not None or _cars_sent != c.cars:
        conf["cars"] = c.cars           # garage (up to 50 cars): only at connection and when it changed
        if client is None:
            _cars_sent = [dict(x) for x in c.cars]
    if client is not None:
        conf.update(laps_m=c.laps_max, time_m=c.time_max // 60000,
                    players_m=c.players_max, gates_m=c.gates_max)
        doc["race"] = {"message": "Welcome on RicinoNext"}
        doc["debug"] = {"message": "RicinoNext " + VERSION}
        hist = {}
        for d in id_data[1: c.players + 1]:
            if d.ID and d.history:
                hist[str(d.ID)] = d.history
        if hist:
            doc["history"] = hist        # a page reloaded during / after a race gets its laps back
        doc["records"] = records.items
        _send_to(client, dumps(doc))
    else:
        text_all(doc)


def _num(obj, key):
    """Firmware reads conf values with atoi(const char*): only JSON strings count."""
    v = obj.get(key)
    if v is None:
        return None
    if not isinstance(v, str):
        print("!! conf.%s sent as %s: the firmware reads strings only, ignored" % (key, type(v).__name__))
        return None
    try:
        return int(v)
    except ValueError:
        return 0                      # atoi("abc") == 0


def _clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def json_to_conf(text):
    """JSONToConf."""
    global raceState, _epoch_offset
    try:
        obj = json.loads(text)["conf"]
    except (ValueError, KeyError, TypeError) as e:
        print("deserializeJson() failed:", e)
        return
    c = ui_config
    v = _num(obj, "state")
    if v is not None:
        raceState = START if v else STOP
        if v:
            stop_read()
    if _num(obj, "reset") is not None:
        raceState = RESET
        c.reset = True
    v = _num(obj, "light")
    if v is not None:
        c.light = v & 0xFF
    v = _num(obj, "laps")
    if v is not None:
        c.laps_condition = _clamp(v, 1, c.laps_max)
    v = _num(obj, "time")
    if v is not None:
        c.time_condition = _clamp(v, 60000, c.time_max)
    v = _num(obj, "solo")
    if v is not None:
        c.solo = bool(v)
    v = _num(obj, "gates")
    if v is not None:
        c.gates = _clamp(v, 1, c.gates_max)
    v = _num(obj, "start")
    if v is not None:
        c.start = FLYING if v else STANDING
    v = _num(obj, "debounce")
    if v is not None:
        c.min_pass_ms = _clamp(v, 1, 10) * 1000
    v = _num(obj, "tolerance")
    if v is not None:
        c.tolerance = _clamp(v, 0, 50)
    v = _num(obj, "warmup")
    if v is not None:
        c.warmup = _clamp(v, c.warmup_min, c.warmup_max)
    v = _num(obj, "players")
    if v is not None:
        c.players = _clamp(v, 1, c.players_max)
    v = _num(obj, "read")
    if v is not None:
        if v and raceState == WAIT:
            start_read()
        else:
            stop_read()
    if "style" in obj:
        try:
            v = int(obj["style"])
            if v in (LAPS, TIME, FREE):
                c.style = v
        except (ValueError, TypeError):
            pass
    v = _num(obj, "now")                 # browser clock (epoch seconds): dates the records
    if v:
        _epoch_offset = v - millis() // 1000
    v = _num(obj, "record_del")
    if v is not None and records.delete(v):
        save_records()
        text_all({"records": records.items})
    cars = obj.get("cars")
    if isinstance(cars, list):
        c.cars = clean_cars(cars)
    names = obj.get("names")
    if isinstance(names, list):
        for i, p in enumerate(names[:NUMBER_RACER_MAX]):   # FIX: bounded
            try:
                c.names[i] = dict(id=int(p.get("id", 0)), name=str(p.get("name", ""))[:15],
                                  color=str(p.get("color", ""))[:7])
            except (ValueError, TypeError, AttributeError):
                pass


# ----------------------------------------------------------------------------
#  Settings persistence (firmware todo: "set/get in EEPROM"), saved 3 s after the last change
# ----------------------------------------------------------------------------
_saved = None
_dirty_at = 0


def settings():
    c = ui_config
    return dict(laps=c.laps_condition, time=c.time_condition, players=c.players, gates=c.gates, style=c.style,
                solo=c.solo, warmup=c.warmup, start=c.start, debounce=c.min_pass_ms // 1000, tolerance=c.tolerance,
                names=[dict(p) for p in c.names], cars=[dict(p) for p in c.cars])


def apply_settings(d):
    c = ui_config
    c.laps_condition = _clamp(int(d.get("laps", c.laps_condition)), 1, c.laps_max)
    c.time_condition = _clamp(int(d.get("time", c.time_condition)), 60000, c.time_max)
    c.players = _clamp(int(d.get("players", c.players)), 1, c.players_max)
    c.gates = _clamp(int(d.get("gates", c.gates)), 1, c.gates_max)
    if d.get("style") in (LAPS, TIME, FREE):
        c.style = d["style"]
    c.solo = bool(d.get("solo", c.solo))
    c.warmup = _clamp(int(d.get("warmup", c.warmup)), c.warmup_min, c.warmup_max)
    c.start = FLYING if d.get("start", c.start) else STANDING
    c.min_pass_ms = _clamp(int(d.get("debounce", c.min_pass_ms // 1000)), 1, 10) * 1000
    c.tolerance = _clamp(int(d.get("tolerance", c.tolerance)), 0, 50)
    for i, p in enumerate(d.get("names", [])[:NUMBER_RACER_MAX]):
        c.names[i] = dict(id=int(p["id"]), name=str(p["name"])[:15], color=str(p["color"])[:7])
    if isinstance(d.get("cars"), list):
        c.cars = clean_cars(d["cars"])


def clean_cars(cars):
    """Garage list from a client or a file: unique codes, short names, at most CARS_MAX."""
    out, seen = [], []
    for p in cars:
        try:
            ID = int(p.get("id", 0))
        except (ValueError, TypeError, AttributeError):
            continue
        if ID <= 0 or ID in seen or ID > 0xFFFFFFFF:
            continue
        seen.append(ID)
        out.append(dict(id=ID, name=str(p.get("name", "")).strip()[:20] or "Voiture %d" % (len(out) + 1)))
        if len(out) >= CARS_MAX:
            break
    return out


def seed_garage():
    """First start with a garage: create a car for each code the drivers already use."""
    c = ui_config
    if c.cars:
        return
    cars = []
    for p in c.names[: c.players]:
        if p["id"]:
            cars.append(dict(id=p["id"], name="Voiture %d" % (len(cars) + 1)))
    c.cars = clean_cars(cars)


def car_of(ID):
    for car in ui_config.cars:
        if car["id"] == ID:
            return car["name"]
    return ""


def save_loop():
    global _dirty_at, _saved
    if _dirty_at and millis() - _dirty_at > 3000 * SPEED:
        _dirty_at = 0
        current = settings()
        if current != _saved and _save_settings:
            try:
                _save_settings(current)
                _saved = current
                print("settings saved")
            except Exception as e:
                print("settings not saved:", e)


def request_state(start):
    """Start / stop from outside the web page (physical button): same effect as the UI, then broadcast."""
    global raceState
    if start and raceState == WAIT:
        stop_read()
        raceState = START
        print("start (button)")
    elif not start and START <= raceState <= FINISH:
        raceState = STOP
        print("stop (button)")
    else:
        return
    conf_to_json()


# ----------------------------------------------------------------------------
#  Arcade records: every finished race / session is offered to the top-10 tables
# ----------------------------------------------------------------------------
def name_of(ID):
    for p in ui_config.names:
        if p["id"] == ID:
            return p["name"], p["color"]
    return "#%d" % ID, ""


def record_race():
    c = ui_config
    at = (_epoch_offset + millis() // 1000) if _epoch_offset is not None else 0
    results = []
    for d in id_data[1: (1 if c.solo else c.players) + 1]:
        if not d.ID or d.laps < 1:
            continue
        name, color = name_of(d.ID)
        results.append(dict(at=at, id=d.ID, name=name, color=color, car=car_of(d.ID), laps=d.laps, best=d.best_lap_time,
                            mean=d.mean_lap_time, total=d.last_total_time - d.start_time,
                            race_laps=c.laps_condition if c.style == LAPS else 0))
    if not results:
        return
    news = records.add(results)
    save_records()
    text_all({"records": records.items, "new": news})
    for n in news:
        print("record: %s %s #%d %d" % (n["name"], n["kind"], n["rank"], n["value"]))


def save_records():
    if _save_records:
        try:
            _save_records(records.dump())
        except Exception as e:
            print("records not saved:", e)


def on_message(text):
    """A WebSocket text message from a client: apply, then broadcast the new conf."""
    global _dirty_at
    json_to_conf(text)
    conf_to_json()
    if settings() != _saved:
        _dirty_at = millis() or 1


def write_json_live():
    """Send one changed racer per loop (like the firmware)."""
    for rank in range(1, ui_config.players + 1):
        d = id_data[rank]
        if d.position_change or d.have_update:
            d.position_change = d.have_update = False
            n = ui_config.gates
            # sectors in track order: gate0->gate1, gate1->gate2, ..., last gate->gate0
            gates = [d.last_check_point[(i + 1) % n] for i in range(n)]
            text_all({"live": dict(rank=rank, id=d.ID, lap=d.laps, best=d.best_lap_time,
                                   last=d.last_lap_time, mean=d.mean_lap_time,
                                   total=d.last_total_time - d.start_time,   # own time since first crossing
                                   cross=d.last_total_time,                  # race time of the last finish-line crossing
                                   sector=gate_index(d.current_gate),        # index of the last gate passed (0 = finish line)
                                   miss=d.missed, cut=d.cuts,                # tolerated skipped gates, laps not counted
                                   gate=gates)})
            if VERBOSE:
                print("live  #%d id=%d lap=%d last=%d best=%d" % (rank, d.ID, d.laps, d.last_lap_time, d.best_lap_time))
            break


_race_last_millis = 0
_race_old_state = None


def write_json_race():
    global _race_last_millis, _race_old_state
    ms = millis()
    delay = (1000 if START <= raceState <= FINISH else 5000) * SPEED
    changed = _race_old_state != raceState
    if ms - _race_last_millis >= delay or changed or race.message is not None:
        _race_old_state = raceState
        _race_last_millis = ms
        doc = dict(state=race_state_char[raceState], lap=race.biggest_lap, time=race.get_current_time())
        if raceState == WARMUP:
            doc["countdown"] = max(0, ui_config.warmup * 1000 - (millis() - race.delay_warmup_timer))
        msg = race.get_message()
        if msg:
            doc["message"] = msg
        text_all({"race": doc})
        if changed:
            print("state", race_state_char[raceState])


_debug_last = 0
_worst_loop = 0


def write_json_debug():
    global _debug_last, _worst_loop
    _worst_loop = max(_worst_loop, _elapsed_us())
    if millis() - _debug_last > 5 * 1000 * SPEED:
        _debug_last = millis()
        g = gate_data[0]
        gates = []
        for x in gate_data:
            if x.address:
                gates.append("%d:%s err d%d q%d l%d%s%s" % (
                    x.address, "RACE" if x.receiver_state == 3 else "IDLE" if x.receiver_state == 1 else x.receiver_state,
                    x.err_decoder, x.err_queue, x.err_link,
                    " DECODER?" if x.flags & 1 else "", " LINK?" if x.flags & 2 else ""))
        doc = dict(time=_worst_loop, memory=_mem_free(), currentTime=g.current_time,
                   state=g.receiver_state, deltaOffsetGate=g.delta_offset_gate,
                   bytesInBuffer=g.bytes_in_buffer, loopTime=g.loop_time, gates=" | ".join(gates),
                   color="#%06X" % random.getrandbits(24))
        if platform_stats:
            try:
                doc.update(platform_stats())
            except Exception as e:
                doc["stats_error"] = str(e)
        text_all({"debug": doc})
        _worst_loop = 0


# ----------------------------------------------------------------------------
#  Main loop, same order as the firmware
# ----------------------------------------------------------------------------
def loop():
    race.loop()
    read_loop()
    save_loop()
    write_json_live()
    write_json_race()
    write_json_debug()
    for hook in loop_hooks:
        hook()
    request_gate()
