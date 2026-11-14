"""Builds the Talea Max for Live devices.

    python m4l/device/build.py [out folder, default m4l/build]

Writes, into the out folder:
  Talea Sequencer.amxd, Talea Track Receiver.amxd       Max MIDI Effects Live loads as they are
  (and a readable .maxpat of each)
  jezric_talea.js, jezric_talea_core.js                 the devices' script, copied from here
  jezric_talea_engine.js                                Talea's engine as WebAssembly, copied
                                                        from m4l/engine/build (python m4l/engine/build.py)

The devices find the scripts in their own folder. The patches are generated here rather
than saved from Max, so they can be reviewed and rebuilt; opening one in Max to look at
it is fine, but changes belong in this file.
"""

import json
import re
import shutil
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
M4L = HERE.parent
ENGINE = M4L / "engine" / "build" / "talea_engine.js"

BRAIN_NAME = "Talea Sequencer"
RECEIVER_NAME = "Talea Track Receiver"
SCRIPT = "jezric_talea.js"
BUS = "jezric_talea_bus"  # the script's BUS: the [r] the brain's messages arrive on
SAVE_PAUSE_MS = 500  # the brain stores the pattern once edits pause this long
DEVICE_WIDTH = 320  # at least; wider when the controls need it (Patch.width)
TRACKS = 16
CORE = M4L.parent / "src" / "core"

# The sequencer's dials in two rows of sections, as on the Rack module's panel: (engine
# key, label). Ranges, defaults and value names come from the engine's own tables
# (Params.hpp, Values.hpp).
DIAL_ROWS = [
    [("SHAPE", [("steps", "Steps"), ("pulses", "Pulses"), ("rotate", "Rotate"),
                ("division", "Division"), ("length", "Length")]),
     ("REPEATS", [("repeats", "Repeats"), ("ramp", "Ramp"), ("repeatTime", "Time"), ("pace", "Pace")])],
    [("GROOVE", [("velocity", "Velocity"), ("probability", "Prob"), ("sustain", "Sustain"),
                 ("accent", "Accent"), ("timing", "Timing"), ("delay", "Delay")]),
     ("TONAL", [("pitch", "Pitch"), ("voicing", "Voicing"), ("range", "Range")])],
]
# Panel layout (presentation px; the device is 169 tall). Each section is a shaded frame
# with its name down its left side, turned counterclockwise (jezric_talea_label.js).
LEFT_X = 88        # the sections' and pads' column, right of the transport column
COL_X = 6          # the transport column: its controls' left edge and width (room for
COL_W = LEFT_X - 12  # two narrow buttons side by side)
APPLY_W = 18       # the "A" (Apply) button, right of the pad mode menu
DIAL_STEP = 47     # dial to dial
SECTION_GAP = 8    # between sections in a row
FRAME_PAD = 3      # frame edge to dials
LABEL_W = 13       # the name's strip down the frame's left side
TRACKS_Y = 2       # track select, like tabs over the sections, TRACKS to its left
ROW_Y = [24, 82]   # the two rows' frames (live.dial: 44 x 48); the pads go under them
ON_W = 30          # the On button, left of the TRACKS label in the top row
TRACKS_H = 18
PADS_Y = 142       # the step pads, under the sections, the size of the track buttons
PAGE_Y = 104       # the step page tabs and Follow, in the left column
FOLLOW_Y = 122
LABEL_SCRIPT = "jezric_talea_label.js"
DISPLAY_SCRIPT = "jezric_talea_display.js"  # the display's pages and rows, read by the script too
DIAGRAM_SCRIPT = "jezric_talea_diagram.js"  # [v8ui]: the display's diagrams
CELL = 29          # a track select button's width
DISPLAY_X = 560    # the display's place: 8 px right of the sections and tabs
DISPLAY_W = 470
DISPLAY_Y = TRACKS_Y + TRACKS_H + 2  # the display's top: its page tabs sit above it, level with
                                      # the track select tabs and styled as they are
DISPLAY_ROW_Y = DISPLAY_Y + 4  # the display's first row
DISPLAY_ROW_H = 19   # row to row
DISPLAY_LABEL_W = 56  # a row's label, then its control
DIAGRAM_Y = 104      # the diagram band, under the rows (pages have at most 4 rows with one)


def display_spec():
    """The display's pages and rows (jezric_talea_display.js: JSON after module.exports),
    with "@scales" filled in from the engine's scale list."""
    text = (HERE / DISPLAY_SCRIPT).read_text(encoding="utf-8")
    spec = json.loads(text[text.rindex("module.exports =") + len("module.exports ="):].strip().rstrip(";"))
    tonal = (CORE / "Tonal.hpp").read_text(encoding="utf-8")
    scales = re.findall(r'\{"([^"]+)", "[^"]+", ', tonal.split("kScales[] = {", 1)[1].split("};", 1)[0])
    values = (CORE / "Values.hpp").read_text(encoding="utf-8")
    rates = re.findall(r'\{"([^"]*)",', values.split("kRandomRates[] = {", 1)[1].split("};", 1)[0])
    named = {"@scales": scales, "@randomRates": rates}
    for rows in spec["views"].values():
        for row in rows:
            if isinstance(row.get("items"), str):
                row["items"] = named[row["items"]]
    return spec


def engine_params():
    """The track parameters from Params.hpp, with their value names from Values.hpp:
    {key: (min, max, default, unit, snap, names or None)}."""
    values = (CORE / "Values.hpp").read_text(encoding="utf-8")
    lists = {}
    for name, body in re.findall(r"NamedValue (k\w+)\[\] = \{(.*?)\n\};", values, re.S):
        lists[name] = re.findall(r'\{"([^"]*)",', body)
    consts = {k: int(v) for k, v in re.findall(r"constexpr int (k\w+) = (\d+);", values)}
    euclid = (CORE / "Euclid.hpp").read_text(encoding="utf-8")
    consts.update({k: int(v) for k, v in re.findall(r"constexpr int (k\w+) = (\d+);", euclid)})
    env = dict(consts, countOf=len, **lists)
    params = (CORE / "Params.hpp").read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r'\{"(\w+)", "[^"]*", ([^,]+), ([^,]+), ([^,]+), "([^"]*)", (true|false)'
                         r'(?:, TALEA_LIST\((\w+)\))?\}', params):
        key, lo, hi, de, unit, snap, lst = m.groups()
        ev = lambda x: eval(x, {"__builtins__": {}}, env)  # noqa: E731 (constants and countOf only)
        out[key] = (ev(lo), ev(hi), ev(de), unit, snap == "true", lists[lst] if lst else None)
    return out


# Live controls that draw a focus frame once clicked.
FOCUS_FRAMED = {"live.tab", "live.text", "live.menu", "live.numbox", "live.dial", "live.toggle",
                "live.button", "live.slider"}


class Patch:
    def __init__(self):
        self.boxes = []
        self.lines = []
        self.count = 0

    def box(self, maxclass, rect, text=None, inlets=1, outlets=0, outlettype=None,
            present=None, **extra):
        self.count += 1
        bid = f"obj-{self.count}"
        b = {
            "id": bid,
            "maxclass": maxclass,
            "numinlets": inlets,
            "numoutlets": outlets,
            "patching_rect": [float(v) for v in rect],
        }
        if outlets:
            b["outlettype"] = outlettype if outlettype is not None else [""] * outlets
        if text is not None:
            b["text"] = text
        if present is not None:
            b["presentation"] = 1
            b["presentation_rect"] = [float(v) for v in present]
        if maxclass in FOCUS_FRAMED:
            # No frame round a clicked control (it keeps the keyboard focus, unframed).
            b["focusbordercolor"] = [0.0, 0.0, 0.0, 0.0]
        b.update(extra)
        self.boxes.append({"box": b})
        return bid

    def obj(self, text, rect, inlets=1, outlets=1, outlettype=None):
        return self.box("newobj", rect, text, inlets, outlets, outlettype)

    def msg(self, text, rect, present=None):
        return self.box("message", rect, text, 2, 1, [""], present)

    def comment(self, text, rect, present=None):
        return self.box("comment", rect, text, 1, 0, None, present)

    def connect(self, src, dst, outlet=0, inlet=0):
        self.lines.append({"patchline": {"source": [src, outlet], "destination": [dst, inlet]}})

    def param(self, maxclass, rect, present, name, ptype, *, mmin=0, mmax=127, initial=0,
              enum=None, outlets=2, outlettype=None, hidden=False, varname=None, unitstyle=None,
              **extra):
        """A Live parameter on a UI object (live.dial, live.menu, ...). Hidden: not saved
        with the set, recalled, automated or in the undo history."""
        attrs = {
            "parameter_longname": name,
            "parameter_shortname": name,
            "parameter_type": ptype,
            "parameter_initial_enable": 1,
            "parameter_initial": [initial],
        }
        if hidden:
            attrs["parameter_invisible"] = 2
        if unitstyle is not None:
            attrs["parameter_unitstyle"] = unitstyle
        if enum is not None:
            attrs["parameter_enum"] = enum
            attrs["parameter_mmax"] = len(enum) - 1
        else:
            attrs["parameter_mmin"] = float(mmin)
            attrs["parameter_mmax"] = float(mmax)
        label = extra.pop("text", None)  # live.text's label, not a box's text
        bid = self.box(maxclass, rect, None, 1, outlets, outlettype, present,
                       parameter_enable=1, varname=varname or name,
                       saved_attribute_attributes={"valueof": attrs}, **extra)
        if label is not None:
            self.boxes[-1]["box"]["text"] = label
        return bid

    def width(self):
        """The device's width: its controls' right edge plus a margin, so a control added
        to the panel is never cut off."""
        right = [b["box"]["presentation_rect"][0] + b["box"]["presentation_rect"][2]
                 for b in self.boxes if "presentation_rect" in b["box"]]
        return max([DEVICE_WIDTH] + [r + 12 for r in right])

    def patcher(self):
        width = float(self.width())
        return {
            "patcher": {
                "fileversion": 1,
                "appversion": {"major": 9, "minor": 0, "revision": 10,
                               "architecture": "x64", "modernui": 1},
                "classnamespace": "box",
                "rect": [100.0, 100.0, 900.0, 600.0],
                "openinpresentation": 1,
                "default_fontsize": 10.0,
                "default_fontname": "Arial Bold",
                "gridsize": [8.0, 8.0],
                "devicewidth": width,
                "openrect": [0.0, 0.0, width, 169.0],
                "openrectmode": 0,
                "boxes": self.boxes,
                "lines": self.lines,
                "parameters": self.parameters(),
                "latency": 0,
                "is_mpe": 0,
                "external_mpe_tuning_enabled": 0,
                "minimum_live_version": "",
                "minimum_max_version": "",
                "platform_compatibility": 0,
                # Without this, Max logs "a project without a name is like a day
                # without sunshine. fatal." when the device loads.
                "project": {
                    "version": 1, "creationdate": 3877132785, "modificationdate": 3877132785,
                    "viewrect": [0.0, 0.0, 300.0, 500.0], "autoorganize": 1,
                    "hideprojectwindow": 1, "showdependencies": 1, "autolocalize": 0,
                    "contents": {"patchers": {}}, "layout": {}, "searchpath": {},
                    "detailsvisible": 0, "amxdtype": 1835887981, "readonly": 0,
                    "devpathtype": 0, "devpath": ".", "sortmode": 0, "viewmode": 0,
                    "includepackages": 0,
                },
                "autosave": 0,
                "oscreceiveudpport": 0,
            }
        }

    def parameters(self):
        out = {}
        for b in self.boxes:
            box = b["box"]
            if box.get("parameter_enable") or box.get("saved_object_attributes", {}).get("parameter_enable"):
                name = box["saved_attribute_attributes"]["valueof"]["parameter_longname"]
                out[box["id"]] = [name, name, 0]
        out["parameterbanks"] = {"0": {"index": 0, "name": "", "parameters": ["-"] * 8,
                                       "buttons": ["-"] * 8}}
        out["inherited_shortname"] = 1
        return out


def common(p, role):
    """The parts both devices share: start-up, the 16th-note clock, the bus and the
    script. Returns (script, startup trigger, midiout, split, transport), where split is
    a [t l l] whose right outlet gives each tick's "bars beats units" before the script
    gets them."""
    load = p.obj("live.thisdevice", [20, 20, 90, 22], 1, 3, ["bang", "int", "int"])
    trig = p.obj("t b b b", [20, 60, 60, 22], 1, 3, ["bang", "bang", "bang"])
    init = p.msg("init", [20, 100, 40, 22])
    on = p.msg("1", [120, 100, 30, 22])
    metro = p.obj("metro 16n @quantize 16n @active 1", [120, 140, 200, 22], 2, 1, ["bang"])
    transport = p.obj("transport", [120, 180, 80, 22], 2, 9)
    pack = p.obj("pack 0 0 0", [120, 220, 80, 22], 3, 1)
    prep = p.obj("prepend tick", [120, 260, 80, 22])
    bus = p.obj(f"r {BUS}", [340, 260, 120, 22], 0, 1)
    script = p.obj(f"v8 {SCRIPT} {role}", [20, 320, 200, 22], 1, 7)
    midiin = p.obj("midiin", [500, 20, 50, 22], 1, 1, ["int"])
    midiout = p.obj("midiout", [500, 420, 60, 22], 1, 0)

    p.connect(load, trig)
    p.connect(trig, init, 1)
    p.connect(trig, on, 0)
    p.connect(init, script)
    p.connect(on, metro)
    p.connect(metro, transport)
    for i in range(3):
        p.connect(transport, pack, i, i)
    split = p.obj("t l l", [120, 240, 40, 22], 1, 2)
    p.connect(pack, split)
    p.connect(split, prep, 0)
    p.connect(prep, script)
    p.connect(bus, script)
    p.connect(midiin, midiout)

    # Debug lines in the Max Console: a toggle in the patch only (not on the panel).
    dbg = p.box("toggle", [240, 20, 20, 20], None, 1, 1, ["int"])
    dbgmsg = p.obj("prepend debug", [240, 50, 90, 22])
    p.comment("debug log", [265, 20, 60, 20])
    p.connect(dbg, dbgmsg)
    p.connect(dbgmsg, script)
    return script, trig, midiout, split, transport


def status_box(p, script, present):
    s = p.msg("", [20, 380, 300, 22], present)
    p.connect(script, s, 1)


def ui(p, trig, script, ctrl, selector, x):
    """Wire a control: its value goes to the script as `selector value`, and the
    start-up trigger bangs it so the script gets the saved value."""
    prep = p.obj(f"prepend {selector}", [x, 520, 110, 22])
    p.connect(trig, ctrl, 2)
    p.connect(ctrl, prep)
    p.connect(prep, script)


def instance_menu(p, trig, script, x=8, y=30):
    p.comment("Instance", [600, 420, 60, 20], [x, y, 60, 20])
    inst = p.param("live.menu", [600, 440, 60, 15], [x + 62, y + 2, 50, 15], "Instance", 2,
                   enum=["A", "B", "C", "D"], outlets=3, outlettype=["", "", "float"])
    ui(p, trig, script, inst, "instanceSelect", 600)


def receiver():
    """Each step's [coll] entry is a list of MIDI entries "offset offPitch 0 onPitch
    velocity", played through [pipe] at their offsets, and CC values "offset -1 ccNumber
    value glide", ramped there by [line]."""
    p = Patch()
    script, trig, midiout, split, transport = common(p, "receiver")
    fmt = p.obj("midiformat", [20, 420, 80, 22], 7, 1, ["int"])
    p.connect(script, fmt)
    p.connect(fmt, midiout)

    # On each tick, before the script hears of it: work out the step and play what the
    # script wrote for it into [coll]. The script keeps [coll] filled look-ahead steps
    # ahead, so it can be held up without notes being lost. [t i i] then deletes the
    # step, so a stored note plays at most once even if the transport reports a step
    # again.
    unpack = p.obj("unpack 0 0 0", [300, 250, 80, 22], 1, 3)
    step = p.obj("expr (($i1-1)*4+$i2-1)*4+($i3+60)/120", [300, 280, 220, 22], 3, 1)
    once = p.obj("t i i", [300, 300, 40, 22], 1, 2, ["int", "int"])
    remove = p.obj("prepend remove", [380, 300, 90, 22])
    steps = p.obj("coll", [300, 320, 50, 22], 1, 4)
    p.connect(split, unpack, 1)
    for i in range(3):
        p.connect(unpack, step, i, i)
    p.connect(step, once)
    p.connect(once, steps, 1)
    p.connect(once, remove, 0)
    p.connect(remove, steps)
    p.connect(script, steps, 2)

    # Each entry: the delay first (offset in quarter notes at the current tempo, from
    # [transport], which outputs the tempo before the position), then the four MIDI
    # numbers into [pipe]. [pipe] gives them out right to left; [pack] puts them back
    # together when the leftmost arrives, and [zl.iter 2] plays "offPitch 0" then
    # "onPitch velocity".
    entries = p.obj("zl.iter 5", [300, 360, 60, 22], 2, 2)
    order = p.obj("t l l", [300, 390, 40, 22], 1, 2)
    offset = p.obj("zl.nth 1", [360, 420, 60, 22], 2, 2)
    delay = p.obj("expr $f1*60000./$f2", [360, 450, 130, 22], 2, 1)
    rest = p.obj("zl.slice 1", [300, 420, 60, 22], 2, 2)
    pipe = p.obj("pipe 0 0 0 0 0", [300, 480, 120, 22], 5, 4)
    pack = p.obj("pack 0 0 0 0", [300, 510, 100, 22], 4, 1)
    kind = p.obj("route -1", [300, 525, 60, 22], 1, 2)
    pairs = p.obj("zl.iter 2", [300, 540, 60, 22], 2, 2)
    p.connect(transport, delay, 4, 1)
    p.connect(steps, entries)
    p.connect(entries, order)
    p.connect(order, offset, 1)
    p.connect(offset, delay)
    p.connect(delay, pipe, 0, 4)
    p.connect(order, rest, 0)
    p.connect(rest, pipe, 1, 0)
    for i in range(4):
        p.connect(pipe, pack, i, i)
    p.connect(pack, kind)
    p.connect(kind, pairs, 1)
    p.connect(pairs, fmt)
    p.connect(script, pipe, 3, 0)  # "clear" on a stop or jump

    # CC values ("ccNumber value glide" after [route -1]): the controller number goes
    # to [join] first; [line] then moves from where it is to the value over the glide
    # (thousandths of a quarter note, in ms at the current tempo), a step per ms, and
    # each new whole number goes out as "176 number value" (CC on channel 1).
    ccsplit = p.obj("t l l", [520, 540, 40, 22], 1, 2)
    ccnum = p.obj("zl.nth 1", [580, 570, 60, 22], 2, 2)
    ccrest = p.obj("zl.slice 1", [520, 570, 60, 22], 2, 2)
    ccunpack = p.obj("unpack 0 0", [520, 600, 70, 22], 1, 2)
    glide = p.obj("expr $i1*60./$f2", [600, 630, 110, 22], 2, 1)
    target = p.obj("pack 0. 0.", [520, 660, 70, 22], 2, 1)
    line = p.obj("line 0. 1.", [520, 690, 70, 22], 3, 2)
    rnd = p.obj("expr int($f1+0.5)", [520, 720, 110, 22], 1, 1)
    change = p.obj("change -1", [520, 750, 70, 22], 1, 3)
    join = p.obj("join 2 @triggers 1", [520, 780, 110, 22], 2, 1)
    status = p.obj("prepend 176", [520, 810, 80, 22])
    bytes_ = p.obj("iter", [520, 840, 40, 22], 1, 1)
    p.connect(kind, ccsplit, 0)
    p.connect(ccsplit, ccnum, 1)
    p.connect(ccnum, join, 0, 0)
    p.connect(ccsplit, ccrest, 0)
    p.connect(ccrest, ccunpack, 1)
    p.connect(ccunpack, glide, 1)
    p.connect(transport, glide, 4, 1)
    p.connect(glide, target, 0, 1)
    p.connect(ccunpack, target, 0)
    p.connect(target, line)
    p.connect(line, rnd)
    p.connect(rnd, change)
    p.connect(change, join, 0, 1)
    p.connect(join, status)
    p.connect(status, bytes_)
    p.connect(bytes_, midiout)

    p.comment("Talea receiver", [600, 400, 150, 20], [8, 4, 200, 20])
    instance_menu(p, trig, script)
    p.comment("Track", [700, 420, 60, 20], [140, 30, 50, 20])
    trk = p.param("live.numbox", [700, 440, 44, 15], [190, 32, 44, 15], "Track", 1,
                  mmin=1, mmax=TRACKS, initial=1, outlets=2, outlettype=["", "float"])
    ui(p, trig, script, trk, "trackSelect", 720)
    status_box(p, script, [8, 60, 300, 22])
    reset = p.msg("reset", [240, 420, 50, 22], [8, 92, 50, 22])
    p.connect(reset, script)
    return p


def brain():
    """The panel: on the left a column with the On button; next to it two rows of dials
    (Shape and Repeats, Groove and Tonal, as on the Rack module) over the track select
    row; on the right the display's place, for now Instance, Look-ahead and the status
    line."""
    p = Patch()
    script, trig, _, _, _ = common(p, "brain")
    info = engine_params()

    # On: the whole sequencer. A stored parameter, so it's saved, automatable and undone
    # like any other; the script starts it at Live's next launch-quantization boundary.
    # Not banged at load like the others: a bang toggles a live.text. The script reads its
    # value at init instead, by its scripting name "On".
    on = p.param("live.text", [840, 440, 44, 20], [COL_X, TRACKS_Y, ON_W, TRACKS_H], "On", 2, enum=["off", "on"],
                 initial=1, outlets=2, outlettype=["", ""], mode=1, text="On", texton="On")
    prep = p.obj("prepend enable", [840, 520, 110, 22])
    p.connect(on, prep)
    p.connect(prep, script)

    # Track select: the track the dials edit, along the top, named in the left column.
    p.box("live.comment", [840, 570, 60, 18], "TRACKS", 1, 0, None, [COL_X + ON_W + 2, TRACKS_Y + 2, COL_W - ON_W - 2, TRACKS_H],
          fontsize=9.0, fontface=1, textjustification=2)
    etrack = p.param("live.tab", [840, 600, 448, 18], [LEFT_X, TRACKS_Y, CELL * TRACKS, TRACKS_H], "Edit track", 2,
                     enum=[str(i) for i in range(1, TRACKS + 1)], outlets=3, outlettype=["", "", "float"])
    ui(p, trig, script, etrack, "editTrackSelect", 840)

    # The dials, one per track parameter. Each sends "param key value" for the edit
    # track; the script sets them to a track's values ("key value" from outlet 3) when the
    # track changes. Hidden parameters: the values are the pattern's (saved with it); as
    # stored parameters, the script setting them at load would mark the set changed, and
    # each turn would be an undo step of its own besides the pattern's.
    keys = [k for row in DIAL_ROWS for _, section in row for k, _ in section]
    route = p.obj("route " + " ".join(keys), [600, 700, 900, 22], 1, len(keys) + 1)
    p.connect(script, route, 3)
    # The section names take Live's theme colour, at load and when the theme changes.
    colors = p.obj("live.colors", [1300, 300, 80, 22], 1, 2)
    ask = p.msg("live_control_fg", [1300, 270, 100, 22])
    p.connect(trig, ask, 2)
    p.connect(colors, ask, 1)
    p.connect(ask, colors)
    n = 0
    for r, row in enumerate(DIAL_ROWS):
        x0 = LEFT_X
        for title, section in row:
            fx, fy = x0, ROW_Y[r]
            fw = LABEL_W + (len(section) - 1) * DIAL_STEP + 44 + 2 * FRAME_PAD
            p.box("panel", [1300 + n * 10, 20, 40, 40], None, 1, 0, None, [fx, fy, fw, 48 + 2 * FRAME_PAD],
                  bgcolor=[0.5, 0.5, 0.5, 0.12], border=1, rounded=3, background=1,
                  bordercolor=[0.5, 0.5, 0.5, 1.0],  # replaced by Live's theme colour:
                  saved_attribute_attributes={"bordercolor": {"expression": "themecolor.live_control_fg_off"}})
            label = p.box("v8ui", [1300 + n * 10, 360, 14, 54], None, 1, 0, None,
                          [fx + 1, fy, LABEL_W, 48 + 2 * FRAME_PAD], filename=LABEL_SCRIPT, border=0,
                          parameter_enable=0)
            name = p.msg(f"title {title}", [1300 + n * 10, 330, 80, 22])
            p.connect(trig, name, 2)
            p.connect(name, label)
            p.connect(colors, label)
            for i, (key, label) in enumerate(section):
                lo, hi, de, unit, snap, names = info[key]
                x = 600 + n * 70
                kw = dict(hidden=True, varname="dial_" + key, outlets=2, outlettype=["", "float"])
                if key == "repeats":  # the last position is endless, as on the Rack knob
                    names = [str(v) for v in range(int(lo), int(hi))] + ["\u221e"]
                if names:
                    kw.update(enum=names, initial=int(de))
                    ptype = 2
                elif snap:
                    kw.update(mmin=lo, mmax=hi, initial=int(de), unitstyle=7 if unit.strip() == "st" else 0)
                    ptype = 1
                else:
                    kw.update(mmin=lo, mmax=hi, initial=float(de), unitstyle=5 if unit == "%" else 1)
                    ptype = 0
                dial = p.param("live.dial", [x, 760, 44, 48],
                               [fx + LABEL_W + FRAME_PAD + i * DIAL_STEP, fy + FRAME_PAD, 44, 48],
                               label, ptype, **kw)
                setter = p.obj("prepend set", [x, 730, 66, 22])
                p.connect(route, setter, n)
                p.connect(setter, dial)
                prep = p.obj(f"prepend param {key}", [x, 820, 66, 22])
                p.connect(dial, prep)
                p.connect(prep, script)
                n += 1
            x0 += fw + SECTION_GAP

    # The step pads: plain buttons (not parameters), one per step of the page; the script
    # colours them ("pad i r g b a" from outlet 5) as Rack lights its pads, and a press
    # sends "pad i bang", after "padMods shift ctrl" from [modifiers] (polled by the press:
    # [t l b] fires right to left, and [modifiers] its outlets too, so [pack]'s hot inlet,
    # Shift, comes last).
    panel_out = p.obj("route pad page follow mode apply", [1500, 400, 200, 22], 1, 6)
    press_t = p.obj("t l b", [1500, 600, 40, 22], 1, 2)
    mods = p.obj("modifiers", [1560, 630, 70, 22], 1, 5, ["int", "int", "int", "int", "int"])
    modpack = p.obj("pack 0 0", [1560, 660, 60, 22], 2, 1)
    modmsg = p.obj("prepend padMods", [1560, 690, 100, 22])
    p.connect(press_t, mods, 1)
    p.connect(mods, modpack, 0, 0)
    p.connect(mods, modpack, 4, 1)  # Ctrl on Windows, Cmd on the Mac
    p.connect(modpack, modmsg)
    p.connect(modmsg, script)
    p.connect(press_t, script, 0)
    p.connect(script, panel_out, 5)
    which = p.obj("route " + " ".join(str(i) for i in range(16)), [1500, 440, 300, 22], 1, 17)
    p.connect(panel_out, which, 0)
    for i in range(16):
        x = 1500 + i * 60
        pad = p.box("live.text", [x, 520, 24, 18], None, 1, 2, ["", ""],
                    [LEFT_X + i * CELL + 1, PADS_Y, CELL - 2, TRACKS_H],
                    parameter_enable=0, mode=0, texton="")
        p.boxes[-1]["box"]["text"] = ""  # a live.text's label (box() takes text as the box's)
        colour = p.obj("prepend activebgcolor", [x, 480, 90, 22])
        p.connect(which, colour, i)
        p.connect(colour, pad)
        press = p.obj(f"prepend pad {i}", [x, 560, 56, 22])
        p.connect(pad, press)
        p.connect(press, press_t)

    # The pad mode menu, beside the pads: Step, Mute, Pattern. Not stored: the script sets
    # it ("mode i" from outlet 5). Choosing Mute again applies the staged mutes, as the "A"
    # (Apply) button does: shown at the menu's right end while there are staged mutes, the
    # menu taking the whole width otherwise.
    mode = p.param("live.menu", [1500, 760, 60, 15], [COL_X, PADS_Y + 1, COL_W, 16], "Pad mode", 2,
                   enum=["Step", "Mute", "Pattern"], outlets=3, outlettype=["", "", "float"], hidden=True,
                   varname="PadMode")
    setmode = p.obj("prepend set", [1500, 730, 70, 22])
    p.connect(panel_out, setmode, 3)
    p.connect(setmode, mode)
    tomode = p.obj("prepend padModeSelect", [1500, 790, 130, 22])
    p.connect(mode, tomode)
    p.connect(tomode, script)
    # Apply: shown only while there are staged mutes ("apply 0/1" from outlet 5 shows or
    # hides it and narrows or widens the menu, by their scripting names, through
    # [thispatcher], on the main thread).
    apply = p.box("live.text", [1640, 760, 24, 16], None, 1, 2, ["", ""],
                  [COL_X + COL_W - APPLY_W, PADS_Y + 1, APPLY_W, 16],
                  parameter_enable=0, mode=0, texton="A", varname="Apply", hidden=1,
                  hint="Apply the staged mutes")
    p.boxes[-1]["box"]["text"] = "A"
    toapply = p.obj("prepend applyMutes", [1640, 790, 110, 22])
    p.connect(apply, toapply)
    p.connect(toapply, script)
    showapply = p.obj("sel 0 1", [1640, 820, 50, 22], 1, 3)
    p.connect(panel_out, showapply, 4)
    menu_at = f"{COL_X} {PADS_Y + 1}"
    hide = p.msg(f"script hide Apply, script sendbox PadMode presentation_rect {menu_at} {COL_W} 16",
                 [1640, 850, 110, 22])
    show = p.msg(f"script show Apply, script sendbox PadMode presentation_rect {menu_at} {COL_W - APPLY_W - 2} 16",
                 [1760, 850, 110, 22])
    p.connect(showapply, hide, 0)
    p.connect(showapply, show, 1)
    applydefer = p.obj("deferlow", [1640, 880, 60, 22])
    p.connect(hide, applydefer)
    p.connect(show, applydefer)
    thispatcher = p.obj("thispatcher", [1640, 910, 80, 22], 1, 2)
    p.connect(applydefer, thispatcher)
    # Blinking: staged mutes, committed ones and a queued pattern.
    blinker = p.obj("metro 125 @active 1", [1760, 760, 120, 22], 2, 1, ["bang"])
    blinkmsg = p.msg("blink", [1760, 790, 50, 22])
    p.connect(blinker, blinkmsg)
    p.connect(blinkmsg, script)

    # The step page tabs (steps 1-16 ... 49-64) and Follow, in the left column. Not stored:
    # the script sets them ("page i", "follow 0/1" from outlet 5).
    page = p.param("live.tab", [1500, 600, 54, 16], [COL_X, PAGE_Y, COL_W, 16], "Step page", 2,
                   enum=["1", "2", "3", "4"], outlets=3, outlettype=["", "", "float"], hidden=True)
    setpage = p.obj("prepend set", [1500, 580, 70, 22])
    p.connect(panel_out, setpage, 1)
    p.connect(setpage, page)
    topage = p.obj("prepend stepPageSelect", [1500, 640, 130, 22])
    p.connect(page, topage)
    p.connect(topage, script)
    follow = p.param("live.text", [1640, 600, 48, 16], [COL_X, FOLLOW_Y, COL_W, 16], "Follow", 2,
                     enum=["off", "on"], initial=1, outlets=2, outlettype=["", ""], hidden=True,
                     mode=1, text="Follow", texton="Follow")
    setfollow = p.obj("prepend set", [1640, 580, 70, 22])
    p.connect(panel_out, setfollow, 2)
    p.connect(setfollow, follow)
    tofollow = p.obj("prepend followSelect", [1640, 640, 120, 22])
    p.connect(follow, tofollow)
    p.connect(tofollow, script)

    display(p, trig, script)

    # "savelater" restarts a [delay]: the pattern is stored once the edits have paused for
    # SAVE_PAUSE_MS, so a dial turned through many values is one step in Live's undo;
    # [deferlow] sends "savepattern" back at low priority.
    split = p.obj("route savelater", [240, 340, 100, 22], 1, 2)
    defer = p.obj("deferlow", [240, 370, 60, 22])
    pause = p.obj(f"delay {SAVE_PAUSE_MS}", [360, 340, 70, 22], 2, 1, ["bang"])
    save = p.msg("savepattern", [360, 370, 80, 22])
    p.connect(script, split, 2)
    p.connect(split, defer, 1)
    p.connect(split, pause, 0)
    p.connect(pause, save)
    p.connect(save, defer)
    p.connect(defer, script)

    # The pattern, saved with the Live set: a Stored Only Live parameter of type Blob
    # (Hidden ones aren't saved). The script stores the pattern in it ("1 p<base64>
    # p<base64>..." from outlet 4); Live saves it with the set and gives it back at load,
    # before the device's init, and on undo and redo. A plain object (not a UI one) takes
    # parameter_enable from saved_object_attributes, as Ableton's devices have it; on the
    # box itself it's ignored and nothing is stored.
    pattr = p.box("newobj", [240, 400, 120, 22], "pattr Pattern", 1, 3, ["", "", ""],
                  varname="Pattern",
                  saved_object_attributes={"parameter_enable": 1},
                  saved_attribute_attributes={"valueof": {
                      "parameter_longname": "Pattern", "parameter_shortname": "Pattern",
                      "parameter_type": 3, "parameter_invisible": 1, "parameter_initial_enable": 0}})
    restore = p.obj("prepend restore", [240, 430, 100, 22])
    p.connect(script, pattr, 4)
    p.connect(pattr, restore)
    p.connect(restore, script)
    return p


def display(p, trig, script):
    """The display: dark, in Live's LCD colours, with the page tabs along its top and each
    page's rows (jezric_talea_display.js) under them, all LCD-style live.* controls. Every
    row starts hidden; the script shows the page's ("script show d_<name>" from outlet 6,
    through [thispatcher]), sets the controls ("d_<name> value"), relabels ("dl_<name>
    text"), renames their choices ("dr_<name> ...") and dims them ("da_<name> 0/1"); a
    control sends "disp <name> value"."""
    spec = display_spec()
    lcd = lambda key: {"expression": f"themecolor.{key}"}  # noqa: E731
    p.box("panel", [1300, 20, 60, 40], None, 1, 0, None, [DISPLAY_X, DISPLAY_Y, DISPLAY_W, 165 - DISPLAY_Y],
          bgcolor=[0.08, 0.08, 0.09, 1.0], bordercolor=[0.3, 0.3, 0.3, 1.0], border=1, rounded=4, background=1,
          saved_attribute_attributes={"bgcolor": lcd("live_lcd_bg"), "bordercolor": lcd("live_lcd_frame")})
    out = p.obj("route script", [2000, 20, 80, 22], 1, 2)
    p.connect(script, out, 6)
    defer = p.obj("deferlow", [2000, 50, 60, 22])
    patcher = p.obj("thispatcher", [2000, 80, 80, 22], 1, 2)
    to_script = p.obj("prepend script", [2100, 50, 90, 22])
    p.connect(out, to_script, 0)
    p.connect(to_script, defer)
    p.connect(defer, patcher)
    names, targets = [], []  # the [route]'s selectors and where each goes

    def label(name, text, rect, x):
        bid = p.box("live.comment", [x, 200, 60, 18], text, 1, 0, None, rect, varname="dl_" + name, hidden=1,
                    fontsize=9.0, textcolor=[0.7, 0.7, 0.7, 1.0],
                    saved_attribute_attributes={"textcolor": lcd("live_lcd_title")})
        setl = p.obj("prepend set", [x, 170, 60, 22])
        p.connect(setl, bid)
        names.append("dl_" + name)
        targets.append(setl)
        return bid

    # The page tabs: not stored (the script sets them).
    pages = p.param("live.tab", [2000, 120, 400, 16], [DISPLAY_X, TRACKS_Y, DISPLAY_W, TRACKS_H], "Display page", 2,
                    enum=spec["pages"], outlets=3, outlettype=["", "", "float"], hidden=True,
                    fontsize=8.0, varname="d_pages")
    setpages = p.obj("prepend set", [2000, 100, 70, 22])
    p.connect(setpages, pages)
    names.append("d_pages")
    targets.append(setpages)
    # A blue frame over the RANDOM tab while the edit track has a random amount (the
    # dials' value arc colour); clicks go through to the tab. Shown by the script.
    tab_w = DISPLAY_W / len(spec["pages"])
    p.box("panel", [2200, 120, 40, 18], None, 1, 0, None,
          [DISPLAY_X + spec["pages"].index("RANDOM") * tab_w, TRACKS_Y, tab_w, TRACKS_H],
          varname="d_rndmark", hidden=1, ignoreclick=1, background=0, border=1, rounded=0,
          bgcolor=[0.0, 0.0, 0.0, 0.0], bordercolor=[0.3, 0.6, 1.0, 1.0],
          saved_attribute_attributes={"bordercolor": lcd("live_value_arc")})
    # A red frame over the CYCLE tab while Cycle edit is on (the dials and step pads edit a
    # cycle), as Rack lights CYC EDIT red.
    p.box("panel", [2250, 120, 40, 18], None, 1, 0, None,
          [DISPLAY_X + spec["pages"].index("CYCLE") * tab_w, TRACKS_Y, tab_w, TRACKS_H],
          varname="d_cycmark", hidden=1, ignoreclick=1, background=0, border=1, rounded=0,
          bgcolor=[0.0, 0.0, 0.0, 0.0], bordercolor=[1.0, 0.25, 0.2, 1.0])
    topage = p.obj("prepend displayPage", [2000, 150, 120, 22])
    p.connect(pages, topage)
    p.connect(topage, script)
    # A page not built yet.
    p.box("live.comment", [2100, 120, 100, 18], "Not in this version yet", 1, 0, None,
          [DISPLAY_X + 10, DISPLAY_ROW_Y, 200, 18], varname="d_soon", hidden=1, fontsize=9.0,
          textcolor=[0.7, 0.7, 0.7, 1.0], saved_attribute_attributes={"textcolor": lcd("live_lcd_title")})

    n = 0
    cx = DISPLAY_X + 8 + DISPLAY_LABEL_W
    full = DISPLAY_X + DISPLAY_W - 8 - cx
    for view, rows in spec["views"].items():
        for r, row in enumerate(rows):
            x = 2000 + n * 90
            # Where it goes: the next row at the left, or the spec's x (from the display's
            # left edge) and row; the label, if any, then the control.
            name, y = row["name"], DISPLAY_ROW_Y + row.get("row", r) * DISPLAY_ROW_H
            lx = DISPLAY_X + 8 + row.get("x", 0)
            lw = row.get("labelW", DISPLAY_LABEL_W)
            if row.get("label"):
                label(name, row["label"], [lx, y, max(lw - 4, 10), 16], x)
            rx = lx + lw
            rect = [rx, y, row.get("width", DISPLAY_X + DISPLAY_W - 8 - rx), 16]
            kind = row["kind"]
            pname = f"{view.title()} {row['label'].title()}" if row.get("label") else f"{view.title()} {name}"
            common = dict(hidden=True, varname="d_" + name, appearance=1)
            if kind == "text":  # a line the script writes ("d_<name> text")
                ctl = p.box("live.comment", [x, 260, 200, 18], "", 1, 0, None, rect, varname="d_" + name,
                            fontsize=9.0, textcolor=[0.85, 0.85, 0.85, 1.0],
                            saved_attribute_attributes={"textcolor": lcd("live_lcd_control_fg")})
                p.boxes[-1]["box"]["hidden"] = 1
                setc = p.obj("prepend set", [x, 230, 70, 22])
                p.connect(setc, ctl)
                names.append("d_" + name)
                targets.append(setc)
                n += 1
                continue
            if kind in ("tab", "buttons"):
                extra = {"button": 1} if kind == "buttons" else {}
                ctl = p.param("live.tab", [x, 260, 80, 16], rect, pname, 2, enum=row["items"], outlets=3,
                              outlettype=["", "", "float"], fontsize=float(row.get("fontsize", 8)), **common, **extra)
            elif kind == "menu":
                ctl = p.param("live.menu", [x, 260, 80, 16], rect, pname, 2, enum=row["items"], outlets=3,
                              outlettype=["", "", "float"], **common)
            else:
                unit = {"unitstyle": 5} if row.get("unit") == "%" else {}
                ctl = p.param("live.numbox", [x, 260, 44, 16], rect, pname, 1, mmin=row["min"], mmax=row["max"],
                              outlets=2, outlettype=["", "float"], **common, **unit)
            p.boxes[-1]["box"]["hidden"] = 1  # shown with its page (hidden= above: a Hidden parameter)
            if row.get("playBars"):
                # A bar under each choice; the script shows the playing one's.
                bw = rect[2] / len(row["items"])
                for i in range(len(row["items"])):
                    p.box("panel", [x + i * 6, 380, 5, 2], None, 1, 0, None,
                          [rect[0] + i * bw + 3, rect[1] + rect[3] - 2, bw - 6, 2],
                          varname=f"d_{name}_play{i}", hidden=1, ignoreclick=1, background=0, border=0,
                          bgcolor=[0.85, 0.85, 0.85, 1.0], saved_attribute_attributes={"bgcolor": lcd("live_lcd_control_fg")})
            setc = p.obj("prepend set", [x, 230, 70, 22])
            p.connect(setc, ctl)
            names.append("d_" + name)
            targets.append(setc)
            act = p.obj("prepend active", [x, 290, 80, 22])
            p.connect(act, ctl)
            names.append("da_" + name)
            targets.append(act)
            if row.get("marks"):  # relabelled choices: "dr_<name> 1 2* 3 ..."
                rng = p.obj("prepend _parameter_range", [x, 350, 90, 22])
                p.connect(rng, ctl)
                names.append("dr_" + name)
                targets.append(rng)
            toscript = p.obj(f"prepend disp {name}", [x, 320, 90, 22])
            p.connect(ctl, toscript)
            p.connect(toscript, script)
            n += 1

    # The diagram band (GROOVE, PHRASE, STYLE): one [v8ui] drawing whichever the page has
    # ("d_diagram groove ...", "d_diagram play x"), in Live's LCD colours; on PHRASE's User
    # shape a click or drag sends "phraseGrid col level".
    diagram = p.box("v8ui", [2000, 470, 200, 54], None, 1, 1, [""],
                    [DISPLAY_X + 8, DIAGRAM_Y, DISPLAY_W - 16, 165 - 4 - DIAGRAM_Y],
                    filename=DIAGRAM_SCRIPT, border=0, parameter_enable=0, varname="d_diagram", hidden=1)
    names.append("d_diagram")
    targets.append(diagram)
    # The board (TRACK's steps, KEYS' keyboard): the same script, under the first row.
    board = p.box("v8ui", [2000, 580, 200, 80], None, 1, 1, [""],
                  [DISPLAY_X + 8, DISPLAY_ROW_Y + DISPLAY_ROW_H, DISPLAY_W - 16, 165 - 4 - DISPLAY_ROW_Y - DISPLAY_ROW_H],
                  filename=DIAGRAM_SCRIPT, border=0, parameter_enable=0, varname="d_board", hidden=1)
    names.append("d_board")
    targets.append(board)
    # Their clicks: "grid col level", "key note", "scale pc".
    clicks = p.obj("route grid key scale", [2000, 530, 130, 22], 1, 4)
    p.connect(diagram, clicks)
    p.connect(board, clicks)
    for i, sel in enumerate(["phraseGrid", "poolKey", "scaleKey"]):
        to = p.obj(f"prepend {sel}", [2000 + i * 110, 560, 100, 22])
        p.connect(clicks, to, i)
        p.connect(to, script)
    dcolors = p.obj("live.colors", [2220, 470, 80, 22], 1, 2)
    dask = p.msg("live_lcd_control_fg, live_lcd_title", [2220, 440, 200, 22])
    p.connect(trig, dask, 2)
    p.connect(dcolors, dask, 1)
    p.connect(dask, dcolors)
    p.connect(dcolors, diagram)
    p.connect(dcolors, board)

    # SETUP's other rows: the device's own controls, after the page's (the script shows
    # them with SETUP: spec "extras").
    base = DISPLAY_ROW_Y + len(spec["views"]["SETUP"]) * DISPLAY_ROW_H + 8
    label("instance", "INSTANCE", [DISPLAY_X + 8, base, DISPLAY_LABEL_W - 4, 16], 1300)
    inst = p.param("live.menu", [600, 440, 60, 15], [cx, base, 50, 16], "Instance", 2,
                   enum=["A", "B", "C", "D"], outlets=3, outlettype=["", "", "float"],
                   varname="d_instance", appearance=1, hidden=False)
    p.boxes[-1]["box"]["hidden"] = 1
    ui(p, trig, script, inst, "instanceSelect", 600)
    label("lookahead", "AHEAD", [DISPLAY_X + 8, base + DISPLAY_ROW_H, DISPLAY_LABEL_W - 4, 16], 1400)
    ahead = p.param("live.numbox", [720, 620, 44, 15], [cx, base + DISPLAY_ROW_H, 44, 16], "Lookahead", 1,
                    mmin=4, mmax=16, initial=4, outlets=2, outlettype=["", "float"], varname="d_lookahead",
                    appearance=1)
    p.boxes[-1]["box"]["hidden"] = 1
    ui(p, trig, script, ahead, "lookahead", 720)
    status = p.msg("", [20, 380, 300, 22], [cx, base + 2 * DISPLAY_ROW_H, full, 18])
    b = p.boxes[-1]["box"]
    b.update(varname="d_status", hidden=1, fontsize=9.0, bgcolor=[0.08, 0.08, 0.09, 1.0], textcolor=[0.7, 0.7, 0.7, 1.0],
             saved_attribute_attributes={"bgcolor": lcd("live_lcd_bg"), "textcolor": lcd("live_lcd_control_fg")})
    p.connect(script, status, 1)

    route = p.obj("route " + " ".join(names), [2000, 400, 900, 22], 1, len(names) + 1)
    p.connect(out, route, 1)
    for i, t in enumerate(targets):
        p.connect(route, t, i)


def amxd(data: bytes) -> bytes:
    """A Max MIDI Effect device file: a small header around the patch JSON."""
    return (b"ampf" + struct.pack("<I", 4) + b"mmmm"
            + b"meta" + struct.pack("<I", 4) + struct.pack("<I", 1)
            + b"ptch" + struct.pack("<I", len(data)) + data)


def main(out):
    out = Path(out)
    if not ENGINE.exists():
        sys.exit(f"{ENGINE} is missing: build the engine first (python m4l/engine/build.py)")
    out.mkdir(parents=True, exist_ok=True)
    for name, build in ((BRAIN_NAME, brain), (RECEIVER_NAME, receiver)):
        text = json.dumps(build().patcher(), indent=1)
        (out / f"{name}.maxpat").write_text(text + "\n", encoding="utf-8")
        (out / f"{name}.amxd").write_bytes(amxd(text.encode("utf-8") + b"\n\x00"))
        print("wrote", out / f"{name}.amxd")
    for src, dst in ((HERE / SCRIPT, SCRIPT), (HERE / "jezric_talea_core.js", "jezric_talea_core.js"),
                     (HERE / LABEL_SCRIPT, LABEL_SCRIPT), (HERE / DISPLAY_SCRIPT, DISPLAY_SCRIPT),
                     (HERE / DIAGRAM_SCRIPT, DIAGRAM_SCRIPT),
                     (ENGINE, "jezric_talea_engine.js")):
        shutil.copyfile(src, out / dst)
        print("copied", out / dst)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else M4L / "build")
