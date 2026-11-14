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
LEFT_X = 60        # the sections' and pads' column, right of the transport column
DIAL_STEP = 47     # dial to dial
SECTION_GAP = 8    # between sections in a row
FRAME_PAD = 3      # frame edge to dials
LABEL_W = 13       # the name's strip down the frame's left side
TRACKS_Y = 2       # track select, like tabs over the sections, TRACKS to its left
ROW_Y = [24, 82]   # the two rows' frames (live.dial: 44 x 48); the pads go under them
ON_Y = 24          # the On button, at the top of the transport column
TRACKS_H = 18
PADS_Y = 142       # the step pads, under the sections, the size of the track buttons
PAGE_Y = 104       # the step page tabs and Follow, in the left column
FOLLOW_Y = 122
APPLY_Y = 84       # Mute mode's Apply, above them; the pad mode menu is beside the pads
LABEL_SCRIPT = "jezric_talea_label.js"
CELL = 29          # a track select button's width
DISPLAY_X = 560    # the display's place


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
    script = p.obj(f"v8 {SCRIPT} {role}", [20, 320, 200, 22], 1, 6)
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
    on = p.param("live.text", [840, 440, 44, 20], [6, ON_Y, 48, 20], "On", 2, enum=["off", "on"],
                 initial=1, outlets=2, outlettype=["", ""], mode=1, text="On", texton="On")
    prep = p.obj("prepend enable", [840, 520, 110, 22])
    p.connect(on, prep)
    p.connect(prep, script)

    # Track select: the track the dials edit, along the top, named in the left column.
    p.box("live.comment", [840, 570, 60, 18], "TRACKS", 1, 0, None, [4, TRACKS_Y + 2, 54, TRACKS_H],
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
    panel_out = p.obj("route pad page follow mode", [1500, 400, 170, 22], 1, 5)
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
    # it ("mode i" from outlet 5). Choosing Mute again applies the staged mutes, as Apply
    # does.
    mode = p.param("live.menu", [1500, 760, 60, 15], [6, PADS_Y + 1, 48, 16], "Pad mode", 2,
                   enum=["Step", "Mute", "Pattern"], outlets=3, outlettype=["", "", "float"], hidden=True)
    setmode = p.obj("prepend set", [1500, 730, 70, 22])
    p.connect(panel_out, setmode, 3)
    p.connect(setmode, mode)
    tomode = p.obj("prepend padModeSelect", [1500, 790, 130, 22])
    p.connect(mode, tomode)
    p.connect(tomode, script)
    apply = p.box("live.text", [1640, 760, 48, 16], None, 1, 2, ["", ""], [6, APPLY_Y, 48, 16],
                  parameter_enable=0, mode=0, texton="Apply")
    p.boxes[-1]["box"]["text"] = "Apply"
    toapply = p.obj("prepend applyMutes", [1640, 790, 110, 22])
    p.connect(apply, toapply)
    p.connect(toapply, script)
    # Blinking: staged mutes, committed ones and a queued pattern.
    blinker = p.obj("metro 125 @active 1", [1760, 760, 120, 22], 2, 1, ["bang"])
    blinkmsg = p.msg("blink", [1760, 790, 50, 22])
    p.connect(blinker, blinkmsg)
    p.connect(blinkmsg, script)

    # The step page tabs (steps 1-16 ... 49-64) and Follow, in the left column. Not stored:
    # the script sets them ("page i", "follow 0/1" from outlet 5).
    page = p.param("live.tab", [1500, 600, 54, 16], [6, PAGE_Y, 48, 16], "Step page", 2,
                   enum=["1", "2", "3", "4"], outlets=3, outlettype=["", "", "float"], hidden=True)
    setpage = p.obj("prepend set", [1500, 580, 70, 22])
    p.connect(panel_out, setpage, 1)
    p.connect(setpage, page)
    topage = p.obj("prepend stepPageSelect", [1500, 640, 130, 22])
    p.connect(page, topage)
    p.connect(topage, script)
    follow = p.param("live.text", [1640, 600, 48, 16], [6, FOLLOW_Y, 48, 16], "Follow", 2,
                     enum=["off", "on"], initial=1, outlets=2, outlettype=["", ""], hidden=True,
                     mode=1, text="Follow", texton="Follow")
    setfollow = p.obj("prepend set", [1640, 580, 70, 22])
    p.connect(panel_out, setfollow, 2)
    p.connect(setfollow, follow)
    tofollow = p.obj("prepend followSelect", [1640, 640, 120, 22])
    p.connect(follow, tofollow)
    p.connect(tofollow, script)

    # Where the display goes (step 3): for now the instance, look-ahead and status.
    p.box("panel", [1300, 20, 60, 40], None, 1, 0, None, [DISPLAY_X, 4, 470, 161],
          bgcolor=[0.0, 0.0, 0.0, 0.25], border=0, rounded=4, background=1)
    p.comment("Talea Sequencer", [600, 400, 150, 20], [DISPLAY_X + 10, 8, 200, 20])
    instance_menu(p, trig, script, x=DISPLAY_X + 10, y=30)
    p.comment("Look-ahead", [720, 600, 70, 20], [DISPLAY_X + 134, 30, 70, 20])
    ahead = p.param("live.numbox", [720, 620, 44, 15], [DISPLAY_X + 204, 32, 44, 15], "Lookahead", 1,
                    mmin=4, mmax=16, initial=4, outlets=2, outlettype=["", "float"])
    ui(p, trig, script, ahead, "lookahead", 720)
    status_box(p, script, [DISPLAY_X + 10, 60, 450, 22])

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
                     (HERE / LABEL_SCRIPT, LABEL_SCRIPT),
                     (ENGINE, "jezric_talea_engine.js")):
        shutil.copyfile(src, out / dst)
        print("copied", out / dst)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else M4L / "build")
