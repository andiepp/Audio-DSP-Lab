"""Draws the diagrams of docs/Implementation-Details.md into docs/images/design,
in the style of docs/images/overview.png and synthesizer-voice.png.

    python tools/Make-DesignFigures.py

Needs Pillow and the Windows font Segoe UI.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'docs', 'images', 'design')

S = 2                                   # drawn at twice the size, then halved
F = 'C:/Windows/Fonts/'
title_font = ImageFont.truetype(F + 'segoeuib.ttf', 25 * S)
head_font = ImageFont.truetype(F + 'segoeuib.ttf', 21 * S)
body_font = ImageFont.truetype(F + 'segoeui.ttf', 17 * S)
label_font = ImageFont.truetype(F + 'segoeui.ttf', 15 * S)

INK, BODY, LINE = (25, 25, 25), (85, 85, 85), (196, 192, 180)
BLUE, BLUE_FILL = (43, 110, 214), (230, 239, 250)
DASH = (205, 205, 205)


class Figure:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new('RGB', (w * S, h * S), 'white')
        self.d = ImageDraw.Draw(self.img)

    def text(self, x, y, s, font=None, fill=BODY, anchor='la'):
        self.d.text((x * S, y * S), s, font=font or label_font, fill=fill, anchor=anchor)

    def title(self, s):
        self.text(40, 34, s, title_font, INK)

    def box(self, x, y, w, h, title, lines, kind='solid'):
        d = self.d
        x, y, w, h = x * S, y * S, w * S, h * S
        if kind == 'accent':
            d.rounded_rectangle((x, y, x + w, y + h), 12 * S, fill=BLUE_FILL, outline=BLUE, width=3 * S)
        elif kind == 'dashed':
            self.dashed(x, y, w, h)
        else:
            d.rounded_rectangle((x, y, x + w, y + h), 12 * S, fill='white', outline=LINE, width=2 * S)
        total = 30 * S + 24 * S * len(lines)
        ty = y + (h - total) // 2
        d.text((x + w // 2, ty), title, font=head_font, fill=INK, anchor='mt')
        for i, line in enumerate(lines):
            d.text((x + w // 2, ty + 32 * S + 24 * S * i), line, font=body_font, fill=BODY, anchor='mt')

    def dashed(self, x, y, w, h):
        d = self.d
        step, on = 12 * S, 7 * S
        for a in range(x + 10 * S, x + w - 10 * S, step):
            d.line((a, y, min(a + on, x + w - 10 * S), y), fill=DASH, width=2 * S)
            d.line((a, y + h, min(a + on, x + w - 10 * S), y + h), fill=DASH, width=2 * S)
        for b in range(y + 10 * S, y + h - 10 * S, step):
            d.line((x, b, x, min(b + on, y + h - 10 * S)), fill=DASH, width=2 * S)
            d.line((x + w, b, x + w, min(b + on, y + h - 10 * S)), fill=DASH, width=2 * S)
        for cx, cy, start in ((x, y, 180), (x + w - 20 * S, y, 270), (x, y + h - 20 * S, 90),
                              (x + w - 20 * S, y + h - 20 * S, 0)):
            d.arc((cx, cy, cx + 20 * S, cy + 20 * S), start, start + 90, fill=DASH, width=2 * S)

    def container(self, x, y, w, h, title):
        self.dashed(x * S, y * S, w * S, h * S)
        self.text(x + 20, y + 14, title, head_font, BODY)

    def arrow(self, points, label=None, label_at=None):
        d = self.d
        pts = [(px * S, py * S) for px, py in points]
        d.line(pts, fill=LINE, width=3 * S, joint='curve')
        (x1, y1), (x2, y2) = pts[-2], pts[-1]
        size = 9 * S
        if x2 > x1:
            tip = [(x2, y2), (x2 - size, y2 - size * 0.6), (x2 - size, y2 + size * 0.6)]
        elif x2 < x1:
            tip = [(x2, y2), (x2 + size, y2 - size * 0.6), (x2 + size, y2 + size * 0.6)]
        elif y2 > y1:
            tip = [(x2, y2), (x2 - size * 0.6, y2 - size), (x2 + size * 0.6, y2 - size)]
        else:
            tip = [(x2, y2), (x2 - size * 0.6, y2 + size), (x2 + size * 0.6, y2 + size)]
        d.polygon(tip, fill=LINE)
        if label:
            lx, ly = label_at
            d.text((lx * S, ly * S), label, font=label_font, fill=BODY, anchor='mm')

    def save(self, name):
        img = self.img.resize((self.w, self.h), Image.LANCZOS)
        path = os.path.join(OUT, name)
        img.save(path, optimize=True)
        print('written', os.path.normpath(path), img.size)


def system():
    f = Figure(1340, 760)
    f.title('Every client speaks one text protocol to the board, which runs the applications')
    f.box(40, 110, 270, 130, 'PC application', ['Windows, .NET 10, WPF;', 'or a terminal, adl.py'])
    f.box(40, 330, 270, 130, 'Front panel', ['CYD: ESP32, touch UI,', 'WiFi bridge, port 7000'])
    f.box(40, 560, 270, 100, 'Internet', ['radio streams'], 'dashed')
    f.arrow([(175, 240), (175, 330)], 'WiFi, TCP', (245, 285))
    f.container(380, 90, 920, 620, 'Audio-DSP-FX board')
    f.box(420, 150, 360, 230, 'ESP32 (PICO kit)', ['core 1: the loop: protocol,', 'applications, I²C, pots', 'core 0: audio tasks, WiFi', 'firmware 1.7'], 'accent')
    f.box(900, 150, 380, 230, 'ADAU1701', ['one of four DSP programs,', 'core at 96 kHz;', '2 ADCs, 4 DACs;', 'boots from the EEPROM'])
    f.box(900, 480, 170, 110, '24C256', ['the DSP image'])
    f.box(1090, 480, 190, 110, 'K2, K3, K4', ['line in,', 'two outputs'])
    f.box(420, 480, 170, 110, 'P1 to P4', ['pots'])
    f.box(610, 480, 170, 110, 'K5', ['MIDI in'])
    f.arrow([(310, 175), (420, 175)], 'USB', (362, 160))
    f.arrow([(310, 395), (355, 395), (355, 330), (420, 330)], 'cable', (334, 418))
    f.arrow([(310, 610), (398, 610), (398, 265), (420, 265)])
    f.arrow([(780, 190), (900, 190)], 'I²C', (840, 176))
    f.arrow([(780, 255), (900, 255)], 'I²S, MCLK', (840, 241))
    f.arrow([(900, 330), (780, 330)], 'I²S', (840, 316))
    f.arrow([(745, 380), (745, 435), (985, 435), (985, 480)], 'I²C: writes the image', (870, 421))
    f.arrow([(1040, 480), (1040, 380)], 'boot', (1066, 455))
    f.arrow([(1150, 480), (1150, 380)])
    f.arrow([(1220, 380), (1220, 480)])
    f.arrow([(505, 480), (505, 380)])
    f.arrow([(695, 480), (695, 380)])
    f.text(40, 728, 'The board owns every value: what one client sets reaches the others as an event.')
    f.save('system.png')


def audio_paths():
    f = Figure(1340, 560)
    f.title("Audio between the ESP32 and the DSP: every clock comes from the ESP32's MCLK")
    f.box(40, 110, 330, 330, 'ESP32', ['I²S port 0, master:', 'MCLK, clocks and data', 'to the DSP', '', 'I²S port 1, slave:', 'the DSP\'s serial output'], 'accent')
    f.box(720, 110, 330, 330, 'ADAU1701', ['serial input', '→ DSP program →', 'serial output (master)', '', 'core: twice per sample'])
    f.box(1120, 110, 180, 120, 'K2', ['line in, ADC0/1'])
    f.box(1120, 320, 180, 120, 'K3, K4', ['DAC0 to DAC3'])
    f.arrow([(370, 160), (720, 160)], 'MCLK 12.288 MHz: GPIO0 → MCLKI', (545, 144))
    f.arrow([(370, 230), (720, 230)], 'BCLK, LRCLK: GPIO27, 14 → MP5, MP4', (545, 214))
    f.arrow([(370, 300), (720, 300)], '16-bit data: GPIO18 or 7 → MP0', (545, 284))
    f.arrow([(720, 390), (370, 390)], '24-bit data, clocks: MP6, 10, 11 → GPIO32, 13, 19', (545, 374))
    f.arrow([(1120, 170), (1050, 170)])
    f.arrow([(1050, 380), (1120, 380)])
    f.text(40, 480, 'Both directions run at 48 kHz from the one MCLK the ESP32 makes, so input and output never drift apart.')
    f.text(40, 512, "The radio runs port 0 at its stream's rate: the DSP's whole clock follows the stream, nothing is resampled.")
    f.save('audio-paths.png')


def receiver_chain():
    f = Figure(1340, 710)
    f.title('From the IF at K2 to the audio at K3: the DSP hands K2 to the ESP32, which does the receiving')
    BW, BH, Y1, Y2 = 228, 112, 120, 330
    xs = [40, 300, 560, 820, 1080]
    f.box(xs[0], Y1, BW - 18, BH, 'K2 in', ['real IF (left) or', 'I and Q, ~1 mV'])
    f.box(xs[1] - 18, Y1, BW + 8, BH, 'ADAU1701', ['ADC0, ADC1 untouched', 'to its serial output'])
    f.box(xs[2] - 2, Y1, BW + 4, BH, 'Down-converter', ['NCO at the IF, DC, I/Q', 'correction, 48 → 12 kHz'])
    f.box(xs[3] + 6, Y1, BW - 6, BH, 'Channel filter', ['up to 511 taps,', 'FFT convolution'], 'accent')
    f.box(xs[4] + 2, Y1, BW - 10, BH, 'AGC, S-meter', ['hang AGC with', 'one block look-ahead'])
    for a, b in ((xs[0] + BW - 18, xs[1] - 18), (xs[1] + BW - 10, xs[2] - 2), (xs[2] + BW + 2, xs[3] + 6),
                 (xs[3] + BW, xs[4] + 2)):
        f.arrow([(a, Y1 + BH // 2), (b, Y1 + BH // 2)])
    f.text((xs[1] + BW - 10 + xs[2] - 2) // 2, Y1 + BH // 2 - 16, 'I²S', anchor='mm')
    f.box(xs[4] + 2, Y2, BW - 10, BH, 'Demodulator', ['AM, USB, LSB,', 'CW, FM'])
    f.box(xs[3] + 6, Y2, BW - 6, BH, 'Noise reduction', ['LMS or spectral'])
    f.box(xs[2] - 2, Y2, BW + 4, BH, 'Squelch', ['mutes below its', 'threshold'])
    f.box(xs[1] - 18, Y2, BW + 8, BH, 'Interpolator', ['12 → 48 kHz,', 'I²S back to the DSP'])
    f.box(xs[0], Y2, BW - 18, BH, 'K3, K4 out', ['equalizer and', 'volume (DSP)'])
    f.arrow([(xs[4] + BW // 2 - 4, Y1 + BH), (xs[4] + BW // 2 - 4, Y2)])
    for a, b in ((xs[4] + 2, xs[3] + BW), (xs[3] + 6, xs[2] + BW + 2), (xs[2] - 2, xs[1] + BW - 10),
                 (xs[1] - 18, xs[0] + BW - 18)):
        f.arrow([(a, Y2 + BH // 2), (b, Y2 + BH // 2)])
    f.box(300, 500, 760, 130, 'Set from the PC, the front panel or the pots',
          ['IF (±21 kHz), input IF or I/Q, mode, filter edges or width (P1),',
           'AGC, NR level (P2), squelch (P3), volume (P4), I/Q gain and phase'], 'dashed')
    f.text(40, 670, "Down-converter to interpolator: one task on the ESP32's core 0, blocks of 5.3 ms, 15 to 31 % of the core")
    f.save('receiver-chain.png')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    system()
    audio_paths()
    receiver_chain()
