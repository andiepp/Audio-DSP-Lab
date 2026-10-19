"""Draws docs/images/overview.png: what goes where.

Usage:

    python tools/Make-Overview.py [out.png]

Needs Pillow and the Windows fonts Segoe UI (regular and bold). Drawn at
twice the size and scaled down, so lines and text come out smooth.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'docs', 'images', 'overview.png')

WIDTH, HEIGHT = 1340, 680
SCALE = 2
FONTS = 'C:/Windows/Fonts/'

INK = (30, 35, 41)            # titles
TEXT = (95, 107, 122)         # the boxes' text
LINE = (198, 196, 186)        # borders and arrows
ESP_FILL, ESP_LINE = (231, 240, 251), (59, 120, 216)
PANEL_FILL, PANEL_LINE = (230, 247, 244), (47, 176, 160)   # the panel's own teal
WHITE = (255, 255, 255)

image = Image.new('RGB', (WIDTH * SCALE, HEIGHT * SCALE), WHITE)
draw = ImageDraw.Draw(image)


def font(name, size):
    return ImageFont.truetype(FONTS + name, size * SCALE)


TITLE = font('segoeuib.ttf', 23)
HEAD = font('segoeuib.ttf', 21)
BODY = font('segoeui.ttf', 17)
LABEL = font('segoeui.ttf', 14)


def s(*values):
    return [v * SCALE for v in values]


def text_centre(x, y, line, face, colour):
    width = draw.textlength(line, font=face)
    draw.text((x * SCALE - width / 2, y * SCALE), line, font=face, fill=colour)


def dashed_rectangle(box, colour, width=2, dash=8, gap=6):
    x0, y0, x1, y1 = box
    for (a, b) in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        dashed_line(a, b, colour, width, dash, gap)


def dashed_line(a, b, colour, width=2, dash=8, gap=6):
    (x0, y0), (x1, y1) = a, b
    length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
    steps = int(length // (dash + gap)) + 1
    for i in range(steps):
        t0 = i * (dash + gap) / length
        t1 = min((i * (dash + gap) + dash) / length, 1)
        if t0 >= 1:
            break
        draw.line(s(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1),
                  fill=colour, width=width * SCALE)


def box(x0, y0, x1, y1, title, lines, kind='solid'):
    if kind == 'esp':
        draw.rounded_rectangle(s(x0, y0, x1, y1), radius=12 * SCALE, fill=ESP_FILL, outline=ESP_LINE, width=3 * SCALE)
    elif kind == 'panel':
        draw.rounded_rectangle(s(x0, y0, x1, y1), radius=12 * SCALE, fill=PANEL_FILL)
        dashed_rectangle((x0, y0, x1, y1), PANEL_LINE, width=3)
    elif kind == 'dashed':
        dashed_rectangle((x0, y0, x1, y1), LINE)
    else:
        draw.rounded_rectangle(s(x0, y0, x1, y1), radius=12 * SCALE, outline=LINE, width=2 * SCALE)
    centre = (x0 + x1) / 2
    text_centre(centre, y0 + 20, title, HEAD, INK)
    for i, line in enumerate(lines):
        text_centre(centre, y0 + 56 + 26 * i, line, BODY, TEXT)


def head(tip, tail, colour):
    (tx, ty), (bx, by) = tip, tail
    length = ((tx - bx) ** 2 + (ty - by) ** 2) ** 0.5
    ux, uy = (tx - bx) / length, (ty - by) / length
    size, half = 14, 6
    left = (tx - ux * size - uy * half, ty - uy * size + ux * half)
    right = (tx - ux * size + uy * half, ty - uy * size - ux * half)
    draw.polygon([tuple(s(*tip)), tuple(s(*left)), tuple(s(*right))], fill=colour)


def arrow(a, b, colour=LINE, both=True, dashed=False):
    if dashed:
        dashed_line(a, b, colour, width=3)
    else:
        draw.line(s(*a, *b), fill=colour, width=3 * SCALE)
    head(b, a, colour)
    if both:
        head(a, b, colour)


def label(x, y, line, colour=TEXT, centre=True):
    width = draw.textlength(line, font=LABEL)
    left = x * SCALE - width / 2 if centre else x * SCALE
    draw.rectangle((left - 4 * SCALE, y * SCALE - 1 * SCALE, left + width + 4 * SCALE, y * SCALE + 19 * SCALE), fill=WHITE)
    draw.text((left, y * SCALE), line, font=LABEL, fill=colour)


# the title
draw.text(tuple(s(40, 34)), 'The PC or the front panel sets values; the board does all the audio work', font=TITLE, fill=INK)

# the top row: PC, the board's ESP32, the DSP
box(40, 120, 340, 290, 'PC', ['Audio-DSP-Lab application', 'or your own script', '(adl.py, any language)'])
box(500, 120, 830, 290, 'ESP32-PICO-KIT', ['firmware: protocol, pots, MIDI,', 'WiFi radio, synthesizer,', 'filter design, THD and spectrum'], 'esp')
box(990, 120, 1300, 290, 'ADAU1701 DSP', ["runs the application's", 'program from its EEPROM', 'at 48 kHz (core 96 kHz)'])
arrow((340, 205), (500, 205))
label(420, 180, 'USB serial 921600')
arrow((830, 205), (990, 205))
label(910, 180, 'I2C · I2S · MCLK')
label(910, 214, 'serial out: K2')

# the front panel, an option: on its own, and the PC's way in over WiFi
box(40, 420, 340, 610, 'Front panel (option)', ['ESP32-2432S028R "CYD",', 'touch: every application', 'without a PC; WiFi for', 'the PC application'], 'panel')
arrow((190, 292), (190, 418), PANEL_LINE, dashed=True)
label(200, 330, 'WiFi, TCP 7000:', PANEL_LINE, centre=False)
label(200, 352, 'no USB cable', PANEL_LINE, centre=False)
arrow((342, 470), (512, 292), PANEL_LINE)
label(410, 352, 'panel port: UART,', PANEL_LINE)
label(410, 373, '3 wires', PANEL_LINE)

# the board's connectors
bottom = [
    (400, 570, 'WiFi', ['internet radio', '(2.4 GHz)']),
    (585, 755, 'P1 to P4', ['four pots, read', 'by the ESP32']),
    (770, 940, 'K5 MIDI in', ['any keyboard,', '5-pin DIN']),
    (955, 1120, 'K2 in', ['line input', 'L / R']),
    (1135, 1300, 'K3, K4 out', ['two stereo', 'line outputs']),
]
for x0, x1, title, lines in bottom:
    box(x0, 480, x1, 630, title, lines, 'dashed')
arrow((560, 292), (485, 478), both=True)            # WiFi
head((600, 292), (670, 478), LINE)
draw.line(s(670, 478, 600, 292), fill=LINE, width=3 * SCALE)          # the pots, read by the ESP32
head((700, 292), (855, 478), LINE)
draw.line(s(855, 478, 700, 292), fill=LINE, width=3 * SCALE)          # MIDI in
head((1060, 292), (1037, 478), LINE)
draw.line(s(1037, 478, 1060, 292), fill=LINE, width=3 * SCALE)        # K2 into the DSP
head((1217, 478), (1170, 292), LINE)
draw.line(s(1170, 292, 1217, 478), fill=LINE, width=3 * SCALE)        # K3, K4 out of the DSP

image.resize((WIDTH, HEIGHT), Image.LANCZOS).save(OUT, dpi=(144, 144))
print('written', os.path.abspath(OUT))
