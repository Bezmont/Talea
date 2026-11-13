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
              enum=None, outlets=2, outlettype=None, hidden=False):
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
        if enum is not None:
            attrs["parameter_enum"] = enum
            attrs["parameter_mmax"] = len(enum) - 1
        else:
            attrs["parameter_mmin"] = float(mmin)
            attrs["parameter_mmax"] = float(mmax)
        return self.box(maxclass, rect, None, 1, outlets, outlettype, present,
                        parameter_enable=1, varname=name,
                        saved_attribute_attributes={"valueof": attrs})

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
    script = p.obj(f"v8 {SCRIPT} {role}", [20, 320, 200, 22], 1, 5)
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


def instance_menu(p, trig, script):
    p.comment("Instance", [600, 420, 60, 20], [8, 30, 60, 20])
    inst = p.param("live.menu", [600, 440, 60, 15], [70, 32, 50, 15], "Instance", 2,
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
    p = Patch()
    script, trig, _, _, _ = common(p, "brain")
    p.comment("Talea Sequencer", [600, 400, 150, 20], [8, 4, 200, 20])
    instance_menu(p, trig, script)
    p.comment("Look-ahead", [720, 600, 70, 20], [140, 30, 70, 20])
    ahead = p.param("live.numbox", [720, 620, 44, 15], [210, 32, 44, 15], "Lookahead", 1,
                    mmin=4, mmax=16, initial=4, outlets=2, outlettype=["", "float"])
    ui(p, trig, script, ahead, "lookahead", 720)
    status_box(p, script, [8, 60, 300, 22])

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

    # Edits: a track menu and a dial per parameter. Each dial sends "param key value" for
    # the menu's track; the script sets the dials to a track's values ("key value" from
    # outlet 3) when the track changes.
    p.comment("Edit track", [840, 600, 70, 20], [262, 30, 60, 20])
    etrack = p.param("live.menu", [840, 620, 50, 15], [320, 32, 40, 15], "Edit track", 2,
                     enum=[str(i) for i in range(1, TRACKS + 1)], outlets=3, outlettype=["", "", "float"])
    ui(p, trig, script, etrack, "editTrackSelect", 840)
    dials = [("pulses", "Pulses", 0, 64), ("rotate", "Rotate", 0, 63), ("repeats", "Repeats", 0, 49),
             ("pace", "Pace", -100, 100), ("probability", "Prob", -100, 100),
             ("sustain", "Sustain", 0, 100), ("timing", "Timing", -100, 100), ("delay", "Delay", -100, 100)]
    route = p.obj("route " + " ".join(k for k, *_ in dials), [600, 700, 400, 22], 1, len(dials) + 1)
    p.connect(script, route, 3)
    for i, (key, name, lo, hi) in enumerate(dials):
        x = 600 + i * 90
        # Hidden: the values are the pattern's (saved with it); as stored parameters, the
        # script setting them at load would mark the set changed, and each turn would be
        # an undo step of its own besides the pattern's.
        dial = p.param("live.dial", [x, 760, 44, 48], [8 + i * 46, 88, 44, 48], name, 1,
                       mmin=lo, mmax=hi, initial=0, outlets=2, outlettype=["", "float"], hidden=True)
        setter = p.obj("prepend set", [x, 730, 70, 22])
        p.connect(route, setter, i)
        p.connect(setter, dial)
        prep = p.obj(f"prepend param {key}", [x, 820, 90, 22])
        p.connect(dial, prep)
        p.connect(prep, script)
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
                     (ENGINE, "jezric_talea_engine.js")):
        shutil.copyfile(src, out / dst)
        print("copied", out / dst)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else M4L / "build")
