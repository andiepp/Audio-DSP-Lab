"""Builds a PDF from one of the Audio-DSP-Lab documents (docs/Manual.md, docs/Installation.md).

Usage:

    python tools/Build-ManualPdf.py docs/Manual.md build/Manual.pdf

Needs ReportLab (pip install reportlab) and the Windows fonts Segoe UI and
Consolas. The cover shows the document's first # heading, the firmware
from its "firmware X.Y" mention and the date of its "As of" line. Images
are referenced relative to the Markdown file. The Markdown understood is
the documents' own: #, ## (chapter, each on a new page), ### (numbered
section), paragraphs, - and 1. lists, pipe tables, ``` code blocks,
![caption](image) and inline **bold**, `code` and [links](url).
"""
import os
import re
import sys

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Frame, Image, KeepTogether, ListFlowable,
                                ListItem, NextPageTemplate, PageBreak, PageTemplate, Paragraph,
                                Preformatted, Spacer, Table, TableStyle, CondPageBreak)
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.lib.fonts import addMapping
from reportlab.lib.utils import ImageReader

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(HERE, '..', 'docs', 'Manual.md')
OUT = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.splitext(SRC)[0] + '.pdf'
BASE = os.path.dirname(SRC)
COVER = {'date': '', 'firmware': '', 'title': 'Manual'}
PROJECT = 'Audio-DSP-Lab'
HOME = 'github.com/andiepp/Audio-DSP-Lab'

FONTS = r'C:\Windows\Fonts'
pdfmetrics.registerFont(TTFont('UI', os.path.join(FONTS, 'segoeui.ttf')))
pdfmetrics.registerFont(TTFont('UI-B', os.path.join(FONTS, 'segoeuib.ttf')))
pdfmetrics.registerFont(TTFont('UI-I', os.path.join(FONTS, 'segoeuii.ttf')))
pdfmetrics.registerFont(TTFont('UI-BI', os.path.join(FONTS, 'segoeuiz.ttf')))
pdfmetrics.registerFont(TTFont('UI-L', os.path.join(FONTS, 'segoeuil.ttf')))
pdfmetrics.registerFont(TTFont('Mono', os.path.join(FONTS, 'consola.ttf')))
addMapping('UI', 0, 0, 'UI'); addMapping('UI', 1, 0, 'UI-B')
addMapping('UI', 0, 1, 'UI-I'); addMapping('UI', 1, 1, 'UI-BI')

NAVY = colors.HexColor('#1B3A6B')
GOLD = colors.HexColor('#C9A227')
INK = colors.HexColor('#1E2329')
QUIET = colors.HexColor('#5F6B7A')
RULE = colors.HexColor('#C9D1DC')
ZEBRA = colors.HexColor('#F7F9FC')
CODEBG = colors.HexColor('#F3F4F6')

PAGE_W, PAGE_H = A4
MARGIN_X = 20 * mm
FRAME_W = PAGE_W - 2 * MARGIN_X

body = ParagraphStyle('body', fontName='UI', fontSize=9.6, leading=13.6, textColor=INK, spaceAfter=6)
lead = ParagraphStyle('lead', parent=body, fontSize=10.6, leading=15, textColor=INK, spaceAfter=10)
h2 = ParagraphStyle('h2', fontName='UI-B', fontSize=21, leading=26, textColor=NAVY, spaceAfter=4)
h2num = ParagraphStyle('h2num', fontName='UI-L', fontSize=40, leading=44, textColor=GOLD)
h3 = ParagraphStyle('h3', fontName='UI-B', fontSize=12.5, leading=16, textColor=NAVY, spaceBefore=12, spaceAfter=5)
cell = ParagraphStyle('cell', fontName='UI', fontSize=8.4, leading=11, textColor=INK)
cellhead = ParagraphStyle('cellhead', parent=cell, fontName='UI-B', textColor=colors.white)
code = ParagraphStyle('code', fontName='Mono', fontSize=7.9, leading=10.4, textColor=INK)
caption = ParagraphStyle('caption', parent=body, fontName='UI-I', fontSize=8.6, textColor=QUIET, alignment=TA_CENTER, spaceBefore=3, spaceAfter=10)
toc_title = ParagraphStyle('toc_title', parent=h2, spaceAfter=6)


def inline(text):
    """Markdown inline -> ReportLab paragraph markup."""
    text = text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    codes = []

    def keep_code(m):
        codes.append(m.group(1))
        return '\x00%d\x00' % (len(codes) - 1)
    text = re.sub(r'`([^`]+)`', keep_code, text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', text)
    # web links stay links; links within the repository (other documents,
    # anchors) have no target in a PDF and become the bare text
    text = re.sub(r'\[([^\]]+)\]\(((?:https?|mailto):[^)]+)\)', r'<link href="\2" color="#1B3A6B"><u>\1</u></link>', text)
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<i>\1</i>', text)
    text = re.sub(r'\x00(\d+)\x00',
                  lambda m: '<font name="Mono" size="8.4" color="#7A2E12">%s</font>' % codes[int(m.group(1))],
                  text)
    return text


class ManualDoc(BaseDocTemplate):
    def __init__(self, path, **kw):
        super().__init__(path, pagesize=A4, leftMargin=MARGIN_X, rightMargin=MARGIN_X,
                         topMargin=24 * mm, bottomMargin=20 * mm, pageCompression=1, title=PROJECT + ' ' + COVER['title'],
                         author=PROJECT, subject='The Elektor Audio-DSP-FX board on a PC', **kw)
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id='f', leftPadding=0,
                      rightPadding=0, topPadding=0, bottomPadding=0)
        self.addPageTemplates([
            PageTemplate('cover', [frame], onPage=self.draw_cover),
            PageTemplate('normal', [frame], onPageEnd=self.draw_running),
        ])
        self.chapter = ''

    def draw_cover(self, canv, doc):
        canv.saveState()
        canv.setFillColor(NAVY)
        canv.rect(0, PAGE_H - 118 * mm, PAGE_W, 118 * mm, stroke=0, fill=1)
        canv.setFillColor(GOLD)
        canv.rect(0, PAGE_H - 121 * mm, PAGE_W, 3 * mm, stroke=0, fill=1)
        canv.setFillColor(colors.white)
        canv.setFont('UI-L', 14)
        canv.drawString(MARGIN_X, PAGE_H - 40 * mm, PROJECT + ' · Elektor Audio-DSP-FX board')
        canv.setFont('UI-B', 40)
        canv.drawString(MARGIN_X, PAGE_H - 64 * mm, COVER['title'])
        canv.setFont('UI', 13)
        canv.drawString(MARGIN_X, PAGE_H - 78 * mm, 'Six audio applications for the ADAU1701 and ESP32,')
        canv.drawString(MARGIN_X, PAGE_H - 85 * mm, 'run from a PC over USB')
        canv.setFont('UI', 10)
        canv.setFillColor(colors.HexColor('#C9D6EA'))
        facts = []
        if COVER['firmware']:
            facts.append('Firmware ' + COVER['firmware'])
        if COVER['date']:
            facts.append('As of ' + COVER['date'])
        canv.drawString(MARGIN_X, PAGE_H - 104 * mm, '  ·  '.join(facts))
        canv.setFillColor(QUIET)
        canv.setFont('UI', 8.5)
        canv.drawString(MARGIN_X, 14 * mm, HOME)
        canv.restoreState()

    def draw_running(self, canv, doc):
        canv.saveState()
        canv.setStrokeColor(RULE)
        canv.setLineWidth(0.6)
        canv.line(MARGIN_X, PAGE_H - 15 * mm, PAGE_W - MARGIN_X, PAGE_H - 15 * mm)
        canv.line(MARGIN_X, 13 * mm, PAGE_W - MARGIN_X, 13 * mm)
        canv.setFont('UI', 8)
        canv.setFillColor(QUIET)
        canv.drawString(MARGIN_X, PAGE_H - 13 * mm, PROJECT + ' ' + COVER['title'])
        canv.drawRightString(PAGE_W - MARGIN_X, PAGE_H - 13 * mm, self.chapter)
        canv.drawString(MARGIN_X, 9 * mm, HOME + (' · firmware ' + COVER['firmware'] if COVER['firmware'] else ''))
        canv.setFont('UI-B', 8.5)
        canv.setFillColor(NAVY)
        canv.drawRightString(PAGE_W - MARGIN_X, 9 * mm, str(canv.getPageNumber()))
        canv.restoreState()

    def beforeDocument(self):
        self.chapter = ''

    def afterFlowable(self, flowable):
        running = getattr(flowable, 'running', None)
        if running:
            self.chapter = running
        kind = getattr(flowable, 'toc_kind', None)
        if not kind:
            return
        level, text, key = kind
        if level == 0:
            self.chapter = text
        self.canv.bookmarkPage(key)
        self.canv.addOutlineEntry(text, key, level=level, closed=level > 0)
        self.notify('TOCEntry', (level, text, self.page, key))


def heading_block(number, title, key):
    num = Paragraph(number, h2num) if number else Spacer(1, 1)
    t = Table([[num], [Paragraph(title, h2)]], colWidths=[FRAME_W])
    t.setStyle(TableStyle([
        ('LEFTPADDING', (0, 0), (-1, -1), 0), ('RIGHTPADDING', (0, 0), (-1, -1), 0),
        ('TOPPADDING', (0, 0), (-1, -1), 0), ('BOTTOMPADDING', (0, 0), (-1, -1), 2),
        ('LINEBELOW', (0, 1), (-1, 1), 1.6, GOLD),
    ]))
    t.toc_kind = (0, (number + '  ' if number else '') + title, key)
    return t


def make_table(rows):
    ncols = max(len(r) for r in rows)
    rows = [r + [''] * (ncols - len(r)) for r in rows]
    lengths = [max(len(r[c]) for r in rows) for c in range(ncols)]
    weights = [min(max(l, 6), 70) ** 0.85 for l in lengths]
    total = sum(weights)
    widths = [FRAME_W * w / total for w in weights]
    # never narrower than the column's longest word (the header's in bold), so no word breaks
    def longest(c):
        head = max((pdfmetrics.stringWidth(w, 'UI-B', cell.fontSize) for w in rows[0][c].split()), default=0)
        rest = max((pdfmetrics.stringWidth(w, 'UI', cell.fontSize) for r in rows[1:] for w in r[c].split()), default=0)
        return max(head, rest) + 12
    floors = [longest(c) for c in range(ncols)]
    short = [c for c in range(ncols) if widths[c] < floors[c]]
    if short:
        need = sum(floors[c] - widths[c] for c in short)
        spare = sum(widths[c] for c in range(ncols) if c not in short)
        for c in range(ncols):
            widths[c] = floors[c] if c in short else widths[c] * (spare - need) / spare
    data = [[Paragraph(inline(c), cellhead) for c in rows[0]]]
    data += [[Paragraph(inline(c), cell) for c in r] for r in rows[1:]]
    t = Table(data, colWidths=widths, repeatRows=1, hAlign='LEFT')
    style = [
        ('BACKGROUND', (0, 0), (-1, 0), NAVY),
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('LINEBELOW', (0, 0), (-1, -1), 0.4, RULE),
        ('TOPPADDING', (0, 0), (-1, -1), 3.2), ('BOTTOMPADDING', (0, 0), (-1, -1), 3.6),
        ('LEFTPADDING', (0, 0), (-1, -1), 5), ('RIGHTPADDING', (0, 0), (-1, -1), 5),
    ]
    for i in range(1, len(data)):
        if i % 2 == 0:
            style.append(('BACKGROUND', (0, i), (-1, i), ZEBRA))
    t.setStyle(TableStyle(style))
    return t


def make_code(lines):
    pre = Preformatted('\n'.join(lines), code)
    t = Table([[pre]], colWidths=[FRAME_W])
    t.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, -1), CODEBG),
        ('LINEBEFORE', (0, 0), (0, -1), 2, NAVY),
        ('LEFTPADDING', (0, 0), (-1, -1), 8), ('TOPPADDING', (0, 0), (-1, -1), 6),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 6),
    ]))
    return t


def image_dpi(full):
    """The resolution an image declares, or None (then it takes the full width)."""
    try:
        from PIL import Image as PilImage
        with PilImage.open(full) as picture:
            dpi = picture.info.get('dpi')
        return float(dpi[0]) if dpi and dpi[0] > 1 else None
    except Exception:
        return None


def make_image(path, alt):
    full = os.path.join(BASE, path)
    iw, ih = ImageReader(full).getSize()
    # an image with its resolution set (the panel's screenshots) keeps its size
    dpi = image_dpi(full)
    w = min(FRAME_W, iw / dpi * 72) if dpi else FRAME_W
    h = w * ih / iw
    img = Image(full, width=w, height=h)
    return KeepTogether([Spacer(1, 4), img, Paragraph(alt, caption)])


def make_list(items, ordered):
    flow = [ListItem(Paragraph(inline(t), body), leftIndent=14) for t in items]
    return ListFlowable(flow, bulletType='1' if ordered else 'bullet', start='1' if ordered else None,
                        bulletFontName='UI-B' if ordered else 'UI', bulletFontSize=9 if ordered else 8,
                        bulletColor=NAVY, leftIndent=14, bulletDedent=12, spaceAfter=6)


def parse(md):
    lines = md.splitlines()
    story_body = []
    cover_lead = None
    chapter = 0
    section = 0
    i = 0
    para = []
    keyn = [0]

    def key():
        keyn[0] += 1
        return 'k%d' % keyn[0]

    def flush():
        nonlocal cover_lead
        if para:
            text = ' '.join(para)
            para.clear()
            if chapter == 0:
                if not text.startswith('As of'):
                    cover_lead = text
                return
            story_body.append(Paragraph(inline(text), body))

    while i < len(lines):
        line = lines[i]
        if line.startswith('```'):
            flush()
            j = i + 1
            block = []
            while not lines[j].startswith('```'):
                block.append(lines[j]); j += 1
            story_body.append(make_code(block)); story_body.append(Spacer(1, 8))
            i = j + 1
            continue
        if line.startswith('# '):
            flush(); i += 1; continue
        if line.startswith('## '):
            flush()
            title = line[3:].strip()
            numbered = title != 'Sources'
            if numbered:
                chapter += 1
            section = 0
            story_body.append(PageBreak())
            story_body.append(heading_block(str(chapter) if numbered else '', title, key()))
            story_body.append(Spacer(1, 12))
            # the chapter's first paragraph is its lead
            i += 1
            while i < len(lines) and not lines[i].strip():
                i += 1
            if i < len(lines) and lines[i] and lines[i][0] not in '#|-!`1':
                story_body.append(Paragraph(inline(lines[i]), lead))
                i += 1
            continue
        if line.startswith('### '):
            flush()
            section += 1
            title = '%d.%d  %s' % (chapter, section, line[4:].strip())
            p = Paragraph(title, h3)
            p.toc_kind = (1, title, key())
            story_body.append(CondPageBreak(60 * mm))
            story_body.append(p)
            i += 1
            continue
        if line.startswith('|'):
            flush()
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                if not all(re.fullmatch(r'-+', c) for c in cells):
                    rows.append(cells)
                i += 1
            story_body.append(make_table(rows)); story_body.append(Spacer(1, 9))
            continue
        m = re.match(r'!\[([^\]]*)\]\(([^)]+)\)', line)
        if m:
            flush(); story_body.append(make_image(m.group(2), m.group(1))); i += 1; continue
        if re.match(r'(- |\d+\. )', line):
            flush()
            ordered = bool(re.match(r'\d+\. ', line))
            items = []
            while i < len(lines) and re.match(r'(- |\d+\. )', lines[i]):
                items.append(re.sub(r'^(- |\d+\. )', '', lines[i])); i += 1
            story_body.append(make_list(items, ordered))
            continue
        if not line.strip():
            flush(); i += 1; continue
        para.append(line.strip())
        i += 1
    flush()
    return cover_lead, story_body


def main():
    with open(SRC, encoding='utf-8') as f:
        md = f.read()
    m = re.search(r'^As of (\d{4}-\d{2}-\d{2})', md, re.M)
    COVER['date'] = m.group(1) if m else ''
    m = re.search(r'[Ff]irmware (\d+\.\d+)', md)
    COVER['firmware'] = m.group(1) if m else ''
    m = re.search(r'^# (.+)$', md, re.M)
    COVER['title'] = m.group(1).strip() if m else 'Manual'
    cover_lead, body_story = parse(md)

    toc = TableOfContents()
    toc.levelStyles = [
        ParagraphStyle('toc0', fontName='UI-B', fontSize=10.6, leading=13.2, textColor=NAVY,
                       leftIndent=0, firstLineIndent=0, spaceBefore=3),
        ParagraphStyle('toc1', fontName='UI', fontSize=9, leading=10.9, textColor=INK,
                       leftIndent=16, firstLineIndent=0),
    ]
    toc.dotsMinLevel = 0
    toc.tableStyle = TableStyle([
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('LEFTPADDING', (0, 0), (-1, -1), 0), ('RIGHTPADDING', (0, 0), (-1, -1), 0),
        ('TOPPADDING', (0, 0), (-1, -1), 0), ('BOTTOMPADDING', (0, 0), (-1, -1), 1),
    ])

    story = [Spacer(1, 112 * mm)]
    if cover_lead:
        story.append(Paragraph(inline(cover_lead), ParagraphStyle('coverlead', parent=lead, fontSize=12,
                                                                    leading=17, textColor=INK)))
    story.append(Spacer(1, 8 * mm))
    first_image = re.search(r'^!\[[^\]]*\]\(([^)]+)\)', md, re.M)
    if first_image:
        path = os.path.join(BASE, first_image.group(1))
        iw, ih = ImageReader(path).getSize()
        story.append(Image(path, width=FRAME_W, height=FRAME_W * ih / iw))
    story.append(NextPageTemplate('normal'))
    story.append(PageBreak())
    contents = Paragraph('Contents', toc_title)
    contents.running = 'Contents'
    story.append(contents)
    story.append(toc)
    story += body_story

    doc = ManualDoc(OUT)
    doc.multiBuild(story)
    print('written', OUT)


main()
