"""Renders the printed parts in CAD/*.stl as pictures for the manual.

Usage:

    python tools/Render-Cad.py [CAD folder] [image folder]

Defaults: CAD/ and docs/images/mechanical/. Needs numpy and Pillow. Each
part is centred, turned to the view given below and drawn with flat
shading in a depth buffer, with thin outlines at steps and creases, at
twice the size and scaled down so the edges come out smooth. The colours
are the prints'; black prints are drawn in a dark grey so their shape
shows.
"""

import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
CAD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'CAD')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'docs', 'images', 'mechanical')

WIDTH, HEIGHT = 900, 620
SCALE = 2
MARGIN = 30
BACKGROUND = (255, 255, 255)
DARK = (88, 92, 100)            # a black print, lightened so it shows
BLUE = (40, 92, 210)            # the panel case's blue

# file, picture, colour, turn about the vertical axis, tilt (degrees, 90 =
# from above), turned over first (the lid's outside faces down in its file)
VIEWS = [
    ('Audio-Lab-DSP-Base.stl', 'dsp-base.png', DARK, -30, 50, False),
    ('Audio-Lab-DSP-Base.stl', 'dsp-base-left.png', DARK, 70, 18, False),
    ('Audio-Lab-DSP-Base.stl', 'dsp-base-right.png', DARK, -110, 18, False),
    ('Audio-Lab-DSP-Top.stl', 'dsp-top.png', DARK, -30, 40, True),
    ('Audio-Lab-DSP-Top.stl', 'dsp-top-inside.png', DARK, -30, 55, False),
    ('Audio-Lab-PanelCase.stl', 'panel-case.png', BLUE, -30, 30, False),
    ('Audio-Lab-PanelFront.stl', 'panel-front.png', DARK, -20, 40, False),
    ('Audio-Lab-PanelFront.stl', 'panel-front-bottom.png', DARK, -20, 40, True),
]


def load(path):
    data = open(path, 'rb').read()
    count = struct.unpack('<I', data[80:84])[0]
    if 84 + 50 * count != len(data):
        raise ValueError(f'{path}: not a binary STL')
    record = np.dtype([('normal', '<f4', 3), ('vertices', '<f4', (3, 3)), ('attribute', '<u2')])
    triangles = np.frombuffer(data[84:], dtype=record, count=count)['vertices'].astype(np.float64)
    low, high = triangles.reshape(-1, 3).min(0), triangles.reshape(-1, 3).max(0)
    return triangles - (low + high) / 2, high - low


def turn(triangles, around, tilt):
    a, t = np.radians(around), np.radians(tilt)
    about_z = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
    # the viewer looks along -y after the tilt: a tilt of 90 shows the part from above
    about_x = np.array([[1, 0, 0], [0, np.cos(t), -np.sin(t)], [0, np.sin(t), np.cos(t)]])
    return triangles @ about_z.T @ about_x.T


def render(triangles, colour):
    # screen: x right, z up; depth along y (smaller = nearer the viewer at -y)
    edge1 = triangles[:, 1] - triangles[:, 0]
    edge2 = triangles[:, 2] - triangles[:, 0]
    normals = np.cross(edge1, edge2)
    lengths = np.linalg.norm(normals, axis=1)
    keep = lengths > 1e-12
    triangles, normals = triangles[keep], normals[keep] / lengths[keep, None]
    facing = normals[:, 1] < 0                       # towards the viewer at -y
    triangles, normals = triangles[facing], normals[facing]

    light = np.array([-0.45, -0.75, 0.5])
    light /= np.linalg.norm(light)
    brightness = 0.38 + 0.62 * np.clip(normals @ light, 0, 1)
    shades = np.clip(np.array(colour, dtype=float) * (0.55 + 0.75 * brightness[:, None]), 0, 255).astype(np.uint8)

    width, height = WIDTH * SCALE, HEIGHT * SCALE
    flat = triangles[:, :, [0, 2]]
    low, high = flat.reshape(-1, 2).min(0), flat.reshape(-1, 2).max(0)
    size = np.array([WIDTH - 2 * MARGIN, HEIGHT - 2 * MARGIN], dtype=float)
    factor = (size / (high - low)).min() * SCALE
    offset = (np.array([width, height]) - (high - low) * factor) / 2
    points = (flat - low) * factor + offset
    points[:, :, 1] = height - points[:, :, 1]       # z up on the screen
    depths = triangles[:, :, 1]

    # a depth buffer: each pixel takes the nearest triangle covering its centre
    depth = np.full((height, width), np.inf)
    picture = np.empty((height, width, 3), dtype=np.uint8)
    picture[:] = BACKGROUND
    for (p0, p1, p2), (z0, z1, z2), shade in zip(points, depths, shades):
        x_low = max(int(np.floor(min(p0[0], p1[0], p2[0]))), 0)
        x_high = min(int(np.ceil(max(p0[0], p1[0], p2[0]))), width - 1)
        y_low = max(int(np.floor(min(p0[1], p1[1], p2[1]))), 0)
        y_high = min(int(np.ceil(max(p0[1], p1[1], p2[1]))), height - 1)
        if x_low > x_high or y_low > y_high:
            continue
        area = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1])
        if abs(area) < 1e-9:
            continue
        x, y = np.meshgrid(np.arange(x_low, x_high + 1) + 0.5, np.arange(y_low, y_high + 1) + 0.5)
        l0 = ((p1[1] - p2[1]) * (x - p2[0]) + (p2[0] - p1[0]) * (y - p2[1])) / area
        l1 = ((p2[1] - p0[1]) * (x - p2[0]) + (p0[0] - p2[0]) * (y - p2[1])) / area
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not inside.any():
            continue
        z = l0 * z0 + l1 * z1 + l2 * z2
        window = depth[y_low:y_high + 1, x_low:x_high + 1]
        nearer = inside & (z < window)
        window[nearer] = z[nearer]
        picture[y_low:y_high + 1, x_low:x_high + 1][nearer] = shade

    # thin outlines where the depth jumps (outlines, steps) or the shade changes (creases)
    solid = np.isfinite(depth)
    filled = np.where(solid, depth, 1e6)
    shade = picture.astype(int).sum(2)
    step = 0.8                                        # mm
    jump = np.zeros_like(solid)
    for axis in (0, 1):
        d = np.abs(np.diff(filled, axis=axis)) > step
        c = (np.abs(np.diff(shade, axis=axis)) > 45) & solid.take(range(1, solid.shape[axis]), axis=axis)
        both = d | c
        if axis == 0:
            jump[1:, :] |= both
        else:
            jump[:, 1:] |= both
    picture[jump] = (picture[jump] * 0.45).astype(np.uint8)
    return Image.fromarray(picture).resize((WIDTH, HEIGHT), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, picture, colour, around, tilt, over in VIEWS:
        triangles, size = load(os.path.join(CAD, name))
        if over:
            triangles = triangles * np.array([1, -1, -1])
        image = render(turn(triangles, around, tilt), colour)
        image.save(os.path.join(OUT, picture), dpi=(200, 200))   # about 11 cm wide in the PDF
        print(f'{picture}: {name}, {size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} mm, {len(triangles)} triangles')


if __name__ == '__main__':
    main()
