"""Generate the Talea panel SVG and review previews.

Outputs:
  res/Talea.svg                      Rack panel (art + hidden 'components' layer for helper.py)
  docs/panel/Talea_preview.svg       panel with mock widgets
  docs/panel/Talea_preview_ids.svg   same, with spec IDs (K02, B10, ...) for markup
  src/rack/PanelLayout.hpp            widget positions + panel labels for the Rack widget

All coordinates are in mm. Rack panel height is 128.5 mm; width = HP * 5.08 mm.
Element names in the components layer are the C++ enum names from docs/SPEC.md.
"""

import os
import re
import sys

HP = 40
W = HP * 5.08
H = 128.5

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Theme
BG = "#23262b"
BOX_FILL = "#2b2f36"
BOX_STROKE = "#3d434c"
TEXT = "#d8dce2"
DIM = "#8a919c"
ACCENT = "#f08a24"
OUT_FILL = "#3a3f47"

# Component kinds -> helper.py colours
KIND_COLOR = {
    "param": "#ff0000",
    "input": "#00ff00",
    "output": "#0000ff",
    "light": "#ff00ff",
    "widget": "#ffff00",
}

# Widget mock sizes (radius mm)
R_KNOB_L = 5.0
R_KNOB = 4.0
R_TRIM = 3.0
R_BTN = 3.0
R_JACK = 4.2
R_LIGHT = 1.0
PAD = 7.6
TRACK_BTN_W, TRACK_BTN_H = 6.6, 3.2
BOX_MARGIN = 1.5  # min gap between a widget's edge and its parent box border

art = []         # panel artwork (goes into res/Talea.svg)
components = []  # (kind, name, x, y, w, h)  w/h only for widgets
mocks = []       # preview-only widget drawings
ids = []         # preview-only ID tags: (spec_id, x, y)
labels = []      # (x, y, size, text, color, anchor, bold) -- Rack doesn't render SVG <text>
boxes = []       # (x, y, w, h) of section boxes, for the containment check
extents = []     # (spec_id, x0, y0, x1, y1) of every widget, for the containment check


def text(x, y, s, size=2.2, color=TEXT, anchor="middle", weight="normal"):
    labels.append((x, y, size, s, color, anchor, weight == "bold"))
    art.append(
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="Helvetica, Arial, sans-serif" '
        f'font-size="{size}" font-weight="{weight}" fill="{color}" text-anchor="{anchor}">{s}</text>'
    )


def box(x, y, w, h, title=None, fill=BOX_FILL):
    boxes.append((x, y, w, h))
    art.append(
        f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="1.2" '
        f'fill="{fill}" stroke="{BOX_STROKE}" stroke-width="0.3"/>'
    )
    if title:
        text(x + 2, y + 2.9, title, size=2.0, color=ACCENT, anchor="start", weight="bold")


def knob(spec_id, name, x, y, label, r=R_KNOB, label_dy=None):
    components.append(("param", name, x, y, 0, 0))
    mocks.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#111316" stroke="#5a616b" stroke-width="0.3"/>')
    mocks.append(f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y - r + 0.8}" stroke="{TEXT}" stroke-width="0.5"/>')
    if label:
        text(x, y - r - 1.2 if label_dy is None else y + label_dy, label, size=1.9)
    ids.append((spec_id, x, y))
    extents.append((spec_id, x - r, y - r, x + r, y + r))


def button(spec_id, name, x, y, label, light=None, light_id=None, label_dy=None, r=R_BTN):
    components.append(("param", name, x, y, 0, 0))
    mocks.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#181a1e" stroke="#5a616b" stroke-width="0.3"/>')
    if light:
        components.append(("light", light, x, y, 0, 0))
        mocks.append(f'<circle cx="{x}" cy="{y}" r="{r - 0.8 if r < R_BTN else r - 1}" fill="#4a3220"/>')
    if label:
        text(x, y - r - 1.2 if label_dy is None else y + label_dy, label, size=1.8)
    ids.append((spec_id, x, y))
    extents.append((spec_id, x - r, y - r, x + r, y + r))


def jack(spec_id, name, x, y, label, output=False, label_dy=None):
    components.append(("output" if output else "input", name, x, y, 0, 0))
    mocks.append(f'<circle cx="{x}" cy="{y}" r="{R_JACK}" fill="#9aa1ab" stroke="#5a616b" stroke-width="0.3"/>')
    mocks.append(f'<circle cx="{x}" cy="{y}" r="1.9" fill="#0b0c0e"/>')
    if label:
        text(x, y - R_JACK - 1.0 if label_dy is None else y + label_dy, label, size=1.8)
    ids.append((spec_id, x, y))
    extents.append((spec_id, x - R_JACK, y - R_JACK, x + R_JACK, y + R_JACK))


def light(spec_id, name, x, y):
    components.append(("light", name, x, y, 0, 0))
    mocks.append(f'<circle cx="{x}" cy="{y}" r="{R_LIGHT}" fill="#3a2a1a" stroke="#5a616b" stroke-width="0.2"/>')
    ids.append((spec_id, x, y))


def pad(spec_id, n, x, y):
    components.append(("param", f"PAD_PARAM_{n}", x, y, 0, 0))
    components.append(("light", f"PAD_LIGHT_{n}", x, y, 0, 0))
    mocks.append(
        f'<rect x="{x - PAD / 2}" y="{y - PAD / 2}" width="{PAD}" height="{PAD}" rx="1" '
        f'fill="#1a1c20" stroke="#5a616b" stroke-width="0.3"/>'
    )
    ids.append((spec_id, x, y))
    extents.append((spec_id, x - PAD / 2, y - PAD / 2, x + PAD / 2, y + PAD / 2))


def track_button(spec_id, n, x, y):
    """Small lit track-select button; the module draws its number."""
    components.append(("param", f"TRACK_SELECT_PARAM_{n}", x, y, 0, 0))
    components.append(("light", f"TRACK_SELECT_LIGHT_{n}", x, y, 0, 0))
    w, h = TRACK_BTN_W, TRACK_BTN_H
    mocks.append(
        f'<rect x="{x - w / 2}" y="{y - h / 2}" width="{w}" height="{h}" rx="0.6" '
        f'fill="#1a1c20" stroke="#5a616b" stroke-width="0.3"/>'
    )
    mocks.append(f'<text x="{x}" y="{y + 0.7}" font-family="Helvetica, Arial, sans-serif" font-size="1.8" '
                 f'fill="{DIM}" text-anchor="middle">{n}</text>')
    ids.append((spec_id, x, y))
    extents.append((spec_id, x - w / 2, y - h / 2, x + w / 2, y + h / 2))


# ---------------------------------------------------------------- layout

# Background + title
art.append(f'<rect x="0" y="0" width="{W:.2f}" height="{H}" fill="{BG}"/>')
# Title + subtitle centred as one block. Widths are measured in Rack's fonts (Nunito Bold,
# drawn 5% larger, and DejaVu Sans), since PanelLabels draws them, not the SVG.
TITLE_W, SUB_W, TITLE_GAP = 9.73 * 1.05, 23.29, 2.5
title_x = W / 2 - (TITLE_W + TITLE_GAP + SUB_W) / 2
text(title_x, 6.0, "Talea", size=4.0, color=TEXT, anchor="start", weight="bold")
text(title_x + TITLE_W + TITLE_GAP, 6.0, "MULTI-SEQUENCER", size=1.9, color=DIM, anchor="start")

# Every gap between sections (and above the knob rows) is GAP. The left column (TRANSPORT,
# PERFORM under it) ends GAP before the display and the knob rows.
GAP = 2
RIGHT_X = 44                # display and knob rows
LEFT_W = RIGHT_X - GAP - 3  # left column width

# Transport block: down to the bottom of the first knob row, with the recording inputs (SEQ
# mode) under the transport inputs.
box(3, 8, LEFT_W, 66.5, "TRANSPORT")
button("B01", "PLAY_PARAM", 10, 17, "PLAY", light="PLAY_LIGHT")
button("B02", "RESET_PARAM", 22, 17, "RESET")
button("B03", "TAP_PARAM", 34, 17, "TAP")
knob("K01", "TEMPO_PARAM", 14, 30, None, r=R_KNOB_L)
text(22, 29, "TEMPO", size=1.9, anchor="start")
text(22, 32, "24–280", size=1.6, color=DIM, anchor="start")
light("L02", "CLOCK_LIGHT", 35, 30)
text(35, 27.2, "CLK", size=1.6, color=DIM)
jack("J01", "CLOCK_INPUT", 10, 44, None)
jack("J02", "RESET_INPUT", 22, 44, None)
jack("J03", "RUN_INPUT", 34, 44, None)
text(4.5, 39.0, "IN", size=1.6, color=DIM, anchor="start")
text(10, 51.3, "CLK", size=1.8)
text(22, 51.3, "RST", size=1.8)
text(34, 51.3, "RUN", size=1.8)
REC_Y = 65.4
text(4.5, 56.9, "NOTE IN", size=1.6, color=DIM, anchor="start")
button("B58", "REC_PARAM", 34, 56.3, None, light="REC_LIGHT")
button("B59", "OVERDUB_PARAM", 22, 56.3, None, light="OVERDUB_LIGHT")
text(17.6, 56.9, "OVD", size=1.8, anchor="end")
text(29.6, 56.9, "REC", size=1.8, anchor="end")
jack("J26", "REC_PITCH_INPUT", 10, REC_Y, None)
jack("J27", "REC_GATE_INPUT", 22, REC_Y, None)
jack("J28", "REC_VELOCITY_INPUT", 34, REC_Y, None)
text(10, REC_Y + 7.3, "V/OCT", size=1.8)
text(22, REC_Y + 7.3, "GATE", size=1.8)
text(34, REC_Y + 7.3, "VEL", size=1.8)

# Context display
DX, DY, DW, DH = RIGHT_X, 8, W - RIGHT_X - 3, 47
components.append(("widget", "ContextDisplay", DX, DY, DW, DH))
art.append(
    f'<rect x="{DX}" y="{DY}" width="{DW:.2f}" height="{DH}" rx="1" fill="#0d0f12" '
    f'stroke="{BOX_STROKE}" stroke-width="0.4"/>'
)
ids.append(("D01", DX + 6, DY + DH - 3))

# Display mock content (preview only)
tabs = ["TRACK", "CYCLE", "STEP", "PHRASE", "SCALE", "STYLE", "REPEAT", "GROOVE", "RANDOM", "SETUP", "PATTERN"]
tw = DW / len(tabs)
for i, t in enumerate(tabs):
    tx = DX + i * tw
    sel = i == 0
    mocks.append(
        f'<rect x="{tx + 0.3:.2f}" y="{DY + 0.6}" width="{tw - 0.6:.2f}" height="3.6" rx="0.6" '
        f'fill="{ACCENT if sel else "#1d2026"}"/>'
    )
    mocks.append(
        f'<text x="{tx + tw / 2:.2f}" y="{DY + 3.2}" font-family="Helvetica, Arial, sans-serif" '
        f'font-size="1.6" fill="{"#111" if sel else DIM}" text-anchor="middle">{t}</text>'
    )
# Euclidean ring mock: 16 steps, 5 pulses
import math
cx, cy, rr = DX + 22, DY + 20, 10
hits = {0, 3, 6, 10, 13}
for s in range(16):
    a = -math.pi / 2 + s * 2 * math.pi / 16
    px, py = cx + rr * math.cos(a), cy + rr * math.sin(a)
    fill = ACCENT if s in hits else "#3a3f47"
    mocks.append(f'<circle cx="{px:.2f}" cy="{py:.2f}" r="{1.3 if s in hits else 0.9}" fill="{fill}"/>')
mocks.append(f'<line x1="{cx}" y1="{cy}" x2="{cx}" y2="{cy - rr + 2}" stroke="{TEXT}" stroke-width="0.3"/>')
info = ["TRACK 1   CYCLE 1/4", "STEPS 16  PULSES 5  ROT 0", "DIV 1/16  LEN 1 BAR", "PHRASE Cadence 1  RANGE 7",
        "STYLE Fixed (poly)  VOICING 0", "GROOVE Agogo  ACCENT 40%"]
for i, line in enumerate(info):
    mocks.append(
        f'<text x="{DX + 40}" y="{DY + 10 + i * 3.6:.2f}" font-family="Consolas, monospace" '
        f'font-size="2.1" fill="{TEXT if i else ACCENT}">{line}</text>'
    )
# Mini track overview
for t in range(16):
    mx = DX + 100 + (t % 8) * 6.3
    my = DY + 13 + (t // 8) * 9
    mocks.append(f'<circle cx="{mx:.2f}" cy="{my:.2f}" r="2.4" fill="none" stroke="{ACCENT if t == 0 else "#3a3f47"}" stroke-width="0.4"/>')
    mocks.append(
        f'<text x="{mx:.2f}" y="{my + 5.2:.2f}" font-family="Helvetica, Arial, sans-serif" font-size="1.4" '
        f'fill="{DIM}" text-anchor="middle">{t + 1}</text>'
    )

# Knob rows: 17.5 high, GAP apart, the first GAP under the display. Each row's groups share the
# width from RIGHT_X to the panel edge, GAP apart, at one knob pitch.
ROW_H = 17.5
ROW1_TOP = DY + DH + GAP
ROW2_TOP = ROW1_TOP + ROW_H + GAP
ROW1_Y, ROW2_Y = ROW1_TOP + 11.5, ROW2_TOP + 11.5


def row_pitch(knobs, groups):
    return (W - 3 - RIGHT_X - (groups - 1) * GAP) / knobs


def knob_group(title, x0, y, items, pitch):
    n = len(items)
    box(x0, y - 11.5, n * pitch, ROW_H, title)
    for i, (spec_id, name, label) in enumerate(items):
        knob(spec_id, name, x0 + pitch / 2 + i * pitch, y + 0.5, label)
    return x0 + n * pitch + GAP


P1 = row_pitch(11, 3)
x = knob_group("SHAPE", RIGHT_X, ROW1_Y, [
    ("K02", "STEPS_PARAM", "STEPS"),
    ("K03", "PULSES_PARAM", "PULSES"),
    ("K04", "ROTATE_PARAM", "ROTATE"),
    ("K05", "DIVISION_PARAM", "DIVISION"),
    ("K06", "LENGTH_PARAM", "LENGTH"),
], P1)
x = knob_group("REPEATS", x, ROW1_Y, [
    ("K07", "REPEATS_PARAM", "REPEATS"),
    ("K08", "RAMP_PARAM", "RAMP"),
    ("K09", "REPEAT_TIME_PARAM", "TIME"),
    ("K10", "PACE_PARAM", "PACE"),
], P1)
knob_group("RANDOM", x, ROW1_Y, [
    ("K21", "VARIANCE_PARAM", "VARIANCE"),
    ("K22", "RANDOM_RATE_PARAM", "RATE"),
], P1)
# Second row: PERFORM in the left column under TRANSPORT, GROOVE / TONAL across the rest.
P2 = row_pitch(10, 2)
x = knob_group("GROOVE", RIGHT_X, ROW2_Y, [
    ("K11", "VELOCITY_PARAM", "VELOCITY"),
    ("K12", "PROBABILITY_PARAM", "PROB"),
    ("K13", "SUSTAIN_PARAM", "SUSTAIN"),
    ("K14", "ACCENT_PARAM", "ACCENT"),
    ("K15", "TIMING_PARAM", "TIMING"),
    ("K16", "DELAY_PARAM", "DELAY"),
], P2)
knob_group("TONAL", x, ROW2_Y, [
    ("K17", "PITCH_PARAM", "PITCH"),
    ("K18", "HARMONY_PARAM", "HARMONY"),
    ("K19", "VOICING_PARAM", "VOICING"),
    ("K20", "RANGE_PARAM", "RANGE"),
], P2)
box(3, ROW2_TOP, LEFT_W, ROW_H, "PERFORM")
for x, (sid, name, label) in zip([10, 22, 34], [("B04", "TEMP_PARAM", "TEMP"), ("B05", "ALL_PARAM", "ALL TRACKS"),
                                                ("B06", "RND_ASSIGN_PARAM", "RND ASSIGN")]):
    button(sid, name, x, ROW2_Y + 0.5, None, light=name.replace("_PARAM", "_LIGHT"))
    text(x, ROW2_Y - 4.7, label, size=1.7)

# Mode / edit row: the pad box and OUT, GAP under the knob rows (D3 = offset from the old
# layout, which put this row's top at 95.5).
ROW3_TOP = ROW2_TOP + ROW_H + GAP
D3 = ROW3_TOP - 95.5
MODE_Y = 102 + D3
box(3, ROW3_TOP, 145, 30.5)
# B13 (KEYS) is retired: the display's KEYS page replaced it.
modes = [("B11", "MODE_STEP_PARAM", "STEP"), ("B12", "MODE_CYCLE_PARAM", "CYCLE"),
         ("B14", "MODE_MUTE_PARAM", "MUTE"), ("B15", "MODE_PATTERN_PARAM", "PATTERN")]
# CYC EDIT sits right after CYCLE; the remaining modes move one slot along.
mode_x = [9 + i * 10.5 for i in (0, 1, 3, 4)]
for i, (sid, name, label) in enumerate(modes):
    button(sid, name, mode_x[i], MODE_Y, None, light=f"MODE_LIGHT_{i}")
    text(mode_x[i], MODE_Y + 5.3, label, size=1.7)
text(5, 98.4 + D3, "PAD MODE", size=1.6, color=ACCENT, anchor="start", weight="bold")
button("B16", "CYCLE_EDIT_PARAM", 9 + 2 * 10.5, MODE_Y, None, light="CYCLE_EDIT_LIGHT")
text(9 + 2 * 10.5, MODE_Y + 5.3, "CYC EDIT", size=1.7)
for i, (sid, name, label) in enumerate([("B17", "COPY_PARAM", "COPY"), ("B18", "PASTE_PARAM", "PASTE"),
                                        ("B19", "CLEAR_PARAM", "CLEAR")]):
    x = 85 + i * 9.5
    button(sid, name, x, MODE_Y, None, light=f"{label}_LIGHT")
    text(x, MODE_Y + 5.3, label, size=1.7)

# Step pages (steps 1-16 / 17-32 / 33-48 / 49-64) + follow playhead
R_SMALL = 2.3
art.append(f'<rect x="110.5" y="{96.6 + D3:.2f}" width="36.3" height="13.2" rx="0.8" fill="none" '
           f'stroke="{BOX_STROKE}" stroke-width="0.3"/>')
text(112, 98.9 + D3, "STEP PAGE", size=1.5, color=ACCENT, anchor="start", weight="bold")
for i in range(4):
    x = 114.5 + i * 6.5
    button(f"B{36 + i}", f"STEP_PAGE_PARAM_{i}", x, MODE_Y + 0.6, None,
           light=f"STEP_PAGE_LIGHT_{i}", r=R_SMALL)
    text(x, MODE_Y + 5.3, f"{i * 16 + 1}–{i * 16 + 16}", size=1.35)
button("B41", "FOLLOW_PARAM", 142.3, MODE_Y + 0.6, None, light="FOLLOW_LIGHT", r=R_SMALL)
text(142.3, MODE_Y + 5.3, "FOLLOW", size=1.35)

# Pads: spread evenly inside the pad box (x 3..148) keeping BOX_MARGIN to each border, with a
# row of track-select buttons (numbered 1-16, which also numbers the pads) just above.
TRACK_BTN_Y = 111.9 + D3
PAD_Y = 118.3 + D3
PAD_BOX_L, PAD_BOX_R = 3, 3 + 145
first = PAD_BOX_L + BOX_MARGIN + PAD / 2
last = PAD_BOX_R - BOX_MARGIN - PAD / 2
for n in range(16):
    x = first + n * (last - first) / 15
    assert PAD_BOX_L + BOX_MARGIN <= x - PAD / 2 and x + PAD / 2 <= PAD_BOX_R - BOX_MARGIN, f"pad {n + 1} outside box"
    pad(f"B{20 + n}", n + 1, x, PAD_Y)
    track_button(f"B{42 + n}", n + 1, x, TRACK_BTN_Y)

# The outputs (the inputs live on the IN expander): OUT level with the pad box, GAP right of
# it; the columns keep their place in the box.
IO_X = 3 + 145 + GAP
cols = [c + IO_X - 151 for c in (158, 168.5, 179, 189.5)]
box(IO_X, ROW3_TOP, W - IO_X - 3, 30.5, "OUT", fill=OUT_FILL)
for i, (sid, name, label) in enumerate([("J20", "PITCH_OUTPUT", "PITCH"), ("J21", "GATE_OUTPUT", "GATE"),
                                        ("J22", "VELOCITY_OUTPUT", "VEL"), ("J25", "EOP_OUTPUT", "EOP")]):
    jack(sid, name, cols[i], 106 + D3, label, output=True)
for i, (sid, name, label) in enumerate([("J23", "CLOCK_OUTPUT", "CLK"), ("J24", "RESET_OUTPUT", "RST")]):
    jack(sid, name, cols[i], 119 + D3, label, output=True)
text(cols[2] + 5, 118.4 + D3, "poly 16:", size=1.6, color=DIM)
text(cols[2] + 5, 120.6 + D3, "ch n = track n", size=1.6, color=DIM)


# ---------------------------------------------------------------- checks

def check_containment():
    """Every widget must lie fully inside the panel and inside any box containing its centre."""
    errors = []
    for sid, x0, y0, x1, y1 in extents:
        if x0 < 0 or y0 < 0 or x1 > W or y1 > H:
            errors.append(f"{sid} extends outside the panel")
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        for bx, by, bw, bh in boxes:
            if bx <= cx <= bx + bw and by <= cy <= by + bh:
                if x0 < bx or y0 < by or x1 > bx + bw or y1 > by + bh:
                    errors.append(f"{sid} crosses the border of box at ({bx}, {by})")
    if errors:
        sys.exit("layout errors:\n  " + "\n  ".join(errors))


check_containment()


# ---------------------------------------------------------------- output

def svg_doc(body, px_scale=None):
    size = (f'width="{W * px_scale:.0f}" height="{H * px_scale:.0f}"' if px_scale
            else f'width="{W:.2f}mm" height="{H}mm"')
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        f'{size} viewBox="0 0 {W:.2f} {H}">\n' + "\n".join(body) + "\n</svg>\n"
    )


def components_layer():
    out = ['<g inkscape:groupmode="layer" id="components" inkscape:label="components" style="display:none">']
    for kind, name, x, y, w, h in components:
        c = KIND_COLOR[kind]
        if kind == "widget":
            out.append(f'<rect id="{name}" x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="{c}"/>')
        else:
            out.append(f'<circle id="{name}" cx="{x:.2f}" cy="{y:.2f}" r="1" fill="{c}"/>')
    out.append("</g>")
    return out


def id_tags():
    out = []
    for sid, x, y in ids:
        w = 1.25 * len(sid) + 1.2
        out.append(f'<rect x="{x - w / 2:.2f}" y="{y - 1.6:.2f}" width="{w:.2f}" height="3.2" rx="0.6" fill="#ffe14d" opacity="0.92"/>')
        out.append(
            f'<text x="{x:.2f}" y="{y + 0.8:.2f}" font-family="Consolas, monospace" font-size="2.2" '
            f'font-weight="bold" fill="#111" text-anchor="middle">{sid}</text>'
        )
    return out


def write(path, content):
    path = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print("wrote", os.path.relpath(path, ROOT))


def layout_header(namespace="layout", extra=None, shared=False):
    """C++ positions (mm) for every component, grouped into arrays for NAME_<n> series.
    `shared`: reuse the Pos / Rect / Label types of PanelLayout.hpp."""
    singles, series = [], {}
    for kind, name, x, y, w, h in components:
        m = re.fullmatch(r"(.*)_(\d+)", name)
        if m:
            series.setdefault(m.group(1), []).append((int(m.group(2)), x, y))
        elif kind == "widget":
            singles.append(f"constexpr Rect {name} = {{{x:.2f}f, {y:.2f}f, {w:.2f}f, {h:.2f}f}};")
        else:
            singles.append(f"constexpr Pos {name} = {{{x:.2f}f, {y:.2f}f}};")
    for base, items in series.items():
        items.sort()
        vals = ", ".join(f"{{{x:.2f}f, {y:.2f}f}}" for _, x, y in items)
        singles.append(f"constexpr Pos {base}[{len(items)}] = {{{vals}}};")
    lab = []
    for x, y, size, s, color, anchor, bold in labels:
        c = color.lstrip("#")
        rgb = ", ".join(str(int(c[i:i + 2], 16)) for i in (0, 2, 4))
        al = {"start": 0, "middle": 1, "end": 2}[anchor]
        txt = s.replace("\\", "\\\\").replace('"', '\\"')
        lab.append(f'    {{{x:.2f}f, {y:.2f}f, {size}f, "{txt}", {{{rgb}}}, {al}, {str(bold).lower()}}},')
    types = (["using layout::Pos;", "using layout::Rect;", "using layout::Label;"] if shared else [
        "struct Pos { float x, y; };",
        "struct Rect { float x, y, w, h; };",
        "struct Label { float x, y, size; const char* text; uint8_t rgb[3]; int align; bool bold; };",
    ])
    head = [
        "// Generated by tools/gen_panel.py -- do not edit.",
        "#pragma once",
        '#include "PanelLayout.hpp"' if shared else "#include <cstdint>",
        "",
        "namespace talea {",
        f"namespace {namespace} {{",
        "",
        *types,
        "",
        f"constexpr float kPanelWidth = {W:.2f}f;",
        f"constexpr float kPanelHeight = {H}f;",
        *(extra or []),
        "",
    ]
    tail = [
        "",
        "// align: 0 = left, 1 = centre, 2 = right; y is the text baseline",
        "constexpr Label kLabels[] = {",
        *lab,
        "};",
        "",
        f"}} // namespace {namespace}",
        "} // namespace talea",
        "",
    ]
    return "\n".join(head + singles + tail)


write("res/Talea.svg", svg_doc(art + components_layer()))
write("src/rack/PanelLayout.hpp", layout_header(extra=[
    f"constexpr float kPadSize = {PAD}f;",
    f"constexpr float kTrackButtonW = {TRACK_BTN_W}f;",
    f"constexpr float kTrackButtonH = {TRACK_BTN_H}f;",
]))
scale = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
write("docs/panel/Talea_preview.svg", svg_doc(art + mocks, px_scale=scale))
write("docs/panel/Talea_preview_ids.svg", svg_doc(art + mocks + id_tags(), px_scale=scale))


# ---------------------------------------------------------------- OUT expander (14 HP)

def reset(hp):
    global W
    W = hp * 5.08
    for lst in (art, components, mocks, ids, labels, boxes, extents):
        lst.clear()


reset(14)
art.append(f'<rect x="0" y="0" width="{W:.2f}" height="{H}" fill="{BG}"/>')
text(W / 2, 6.0, "Talea OUT", size=3.4, color=TEXT, weight="bold")
light("X-L17", "LINK_LIGHT", W - 5, 4.9)
text(W - 7.2, 5.6, "LINK", size=1.5, color=DIM, anchor="end")
# Two columns of 8 tracks; each row: number, status light, Pitch / Gate / Vel (poly: one
# channel per chord note).
COL_X = [3, 3 + (W - 6) / 2]
COL_W = (W - 6) / 2
ROW0, ROW_DY = 21.5, 13.6
for c in range(2):
    x0 = COL_X[c]
    art.append(f'<rect x="{x0 + 0.5:.2f}" y="9.5" width="{COL_W - 1:.2f}" height="116" rx="1" fill="{OUT_FILL}"/>')
    jx = [x0 + 10.4, x0 + 19.1, x0 + 27.8]
    for k, name in enumerate(["PITCH", "GATE", "VEL"]):
        text(jx[k], 13.2, name, size=1.6, color=DIM)
    for r in range(8):
        t = c * 8 + r
        y = ROW0 + r * ROW_DY
        text(x0 + 3.3, y - 1.2, str(t + 1), size=2.2, weight="bold")
        light(f"X-L{t + 1:02d}", f"TRACK_LIGHT_{t}", x0 + 3.3, y + 2.2)
        jack(f"X-J{t + 1:02d}", f"PITCH_OUTPUT_{t}", jx[0], y, None, output=True)
        jack(f"X-J{17 + t:02d}", f"GATE_OUTPUT_{t}", jx[1], y, None, output=True)
        jack(f"X-J{33 + t:02d}", f"VELOCITY_OUTPUT_{t}", jx[2], y, None, output=True)
check_containment()
write("res/TaleaOut.svg", svg_doc(art + components_layer()))
write("src/rack/OutLayout.hpp", layout_header(namespace="out_layout", shared=True))
write("docs/panel/TaleaOut_preview.svg", svg_doc(art + mocks, px_scale=scale))
write("docs/panel/TaleaOut_preview_ids.svg", svg_doc(art + mocks + id_tags(), px_scale=scale))


# ---------------------------------------------------------------- IN expander (14 HP)

reset(14)
art.append(f'<rect x="0" y="0" width="{W:.2f}" height="{H}" fill="{BG}"/>')
text(W / 2, 6.0, "Talea IN", size=3.4, color=TEXT, weight="bold")
light("Y-L01", "LINK_LIGHT", W - 5, 4.9)
text(W - 7.2, 5.6, "LINK", size=1.5, color=DIM, anchor="end")
# One poly CV input + attenuverter per modulatable knob parameter (core order).
MOD_PARAMS = ["PULSES", "ROTATE", "REPEATS", "RAMP", "TIME", "PACE", "VELOCITY", "PROB",
              "SUSTAIN", "ACCENT", "TIMING", "DELAY", "PITCH", "VOICING", "RANGE"]
box(3, 8.5, W - 6, 79, "CV → PARAMETER")
cx = [12.1 + c * 16.0 for c in range(4)]
for i, label in enumerate(MOD_PARAMS):
    x, y0 = cx[i % 4], 15.5 + (i // 4) * 18.5
    text(x, y0, label, size=1.6)
    knob(f"Y-K{i + 1:02d}", f"ATTEN_PARAM_{i}", x, y0 + 4.2, None, r=R_TRIM)
    jack(f"Y-J{i + 1:02d}", f"CV_INPUT_{i}", x, y0 + 12.2, None)
text(cx[3], 15.5 + 3 * 18.5 + 5.0, "poly 16:", size=1.6, color=DIM)
text(cx[3], 15.5 + 3 * 18.5 + 7.4, "ch n =", size=1.6, color=DIM)
text(cx[3], 15.5 + 3 * 18.5 + 9.8, "track n", size=1.6, color=DIM)
box(3, 89.5, W - 6, 36.5, "PERFORM")
perform = [("Y-J16", "TRANSPOSE_INPUT", "XPOSE"), ("Y-J17", "PHRASE_INPUT", "PHRASE"),
           ("Y-J18", "PATTERN_INPUT", "PATTERN"), ("Y-J19", "TEMP_INPUT", "TEMP"),
           ("Y-J20", "MUTE_INPUT", "MUTES"), ("Y-J21", "MUTE_APPLY_INPUT", "APPLY"),
           ("Y-J22", "REGEN_INPUT", "REGEN")]
for i, (sid, name, label) in enumerate(perform):
    jack(sid, name, cx[i % 4], 102 + (i // 4) * 16.5, label)
check_containment()
write("res/TaleaIn.svg", svg_doc(art + components_layer()))
write("src/rack/InLayout.hpp", layout_header(namespace="in_layout", shared=True))
write("docs/panel/TaleaIn_preview.svg", svg_doc(art + mocks, px_scale=scale))
write("docs/panel/TaleaIn_preview_ids.svg", svg_doc(art + mocks + id_tags(), px_scale=scale))
