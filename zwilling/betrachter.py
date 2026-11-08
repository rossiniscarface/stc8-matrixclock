#!/usr/bin/env python3
"""Builds the viewer for the digital twin: an HTML page that plays back the
recorded runs (laeufe/*.jsonl) on a replica 25x7 matrix.
Usage: python3 betrachter.py  ->  betrachter.html"""
import json, os, glob

HIER = os.path.dirname(os.path.abspath(__file__))
TITEL = {
    'menue': ('Menu tour', 'Menu in the 5×7 font with the value in plain text: both keys 2.5 s, then on every 3 s to the end.'),
    'wecker_folge': ('Alarm with sequence puzzle', 'Alarm tone; a touch shows six symbols to repeat; a wrong tap flashes, after 20 s without input it rings again.'),
    'wecker_rechnen': ('Alarm with arithmetic', 'Alarm tone; a touch shows a plus or minus problem with a one-digit result; triangle counts 0–9, circle confirms.'),
    'orgel': ('Light organ', 'Matrix black, every noise throws three rings from the centre outwards; touching ends it.'),
    'nacht': ('Night mode', 'Light off: after 10 s the display goes off. Clapping wakes it for 15 s. Light on: normal.'),
    'laufschrift': ('Scrolling text', 'Time, then a scrolling text by command via the ESP, then the time again.'),
}
W = {1:'push left',2:'random pixels',3:'column by column',4:'top to bottom',5:'split from the centre',6:'roll centre',7:'roll up and down',8:'wipe sideways'}
Z = {1:'build column by column',2:'fall',3:'random pixels',4:'up and down',5:'from the centre'}
D = {1:'run to the centre',2:'fade in and out',3:'constant',4:'move up and down',5:'blink every second'}
for n, t in W.items():
    TITEL[f'wechsel{n}'] = (f'Transition {n}', f'Transition animation {n} ({t}): time 5 s, then date and weekday, about 3 s each.')
for n, t in Z.items():
    TITEL[f'ziffer{n}'] = (f'Digit {n}', f'Digit effect {n} ({t}) when seconds and minutes change, theme 1.')
for n, t in D.items():
    TITEL[f'punkt{n}'] = (f'Colon {n}', f'Colon effect {n} ({t}), theme 2.')
TITEL['schrift_t1'] = ('Font 3×7', 'Firmware: FONT 1 = A square, FONT 2 = B rounded (theme 1).')
TITEL['schrift_t2'] = ('Font 5×7', 'Firmware: FONT 1 photo, 2 classic thin, 3 square bold (theme 2).')
TITEL['countdown'] = ('Countdown', 'Countdown by command (C0012): runs large as MM:SS, at 0 the alarm melody, touching ends it.')
TITEL['kalender'] = ('Calendar', 'Three entries by command: two all-day (scroll after the weekday), one appointment at 18:43 with tone, which interrupts the running text.')
TITEL['led'] = ('Rear LEDs', 'Rear-LED effects (bar below the matrix): constant, breathing, follows the sound, off.')
TITEL['helligkeit'] = ('Brightness', 'All pixels on, levels 1 to 8 (2 % to 91 %), then automatic. The matrix in the viewer gets darker accordingly.')
TITEL['textschrift'] = ('Text fonts', 'The same scrolling text in font 2 (thin), 1 and 3 (both bold). Also applies to the menu and weekday.')
TITEL['menue_anim'] = ('Anim demo', 'Menu "Anim": each circle selects the next animation and demonstrates it (menu text to time and back).')
TITEL['menue_timer'] = ('Set timer', 'Menu "Timer": tapping the triangle counts seconds, holding gets faster (seconds, then 10 s, then minutes), circle starts.')
TITEL['menue_schrift'] = ('Choose font', 'Menu "Schrift": preview on the time, triangle switches, only the circle saves.')
TITEL['theme3'] = ('Theme 3', 'Hours and minutes large, colon free, seconds small; with digit effect 4 (up and down).')
GRUPPE = {'textschrift': 'Display mode', 'menue_anim': 'Operation', 'menue_timer': 'Operation', 'menue_schrift': 'Operation', 'countdown': 'New', 'kalender': 'New', 'led': 'New', 'helligkeit': 'New', 'schrift_t1': 'Display mode', 'schrift_t2': 'Display mode', 'menue': 'Operation', 'wecker_folge': 'Operation', 'wecker_rechnen': 'Operation', 'orgel': 'Operation',
          'nacht': 'Operation', 'laufschrift': 'Operation', 'theme3': 'Display mode'}
for n in W: GRUPPE[f'wechsel{n}'] = 'Transition animations'
for n in Z: GRUPPE[f'ziffer{n}'] = 'Digit effects'
for n in D: GRUPPE[f'punkt{n}'] = 'Colon'
REIHE = ['menue', 'menue_anim', 'menue_timer', 'menue_schrift', 'kalender', 'countdown', 'led', 'helligkeit', 'menue', 'wecker_folge', 'wecker_rechnen', 'orgel', 'nacht', 'laufschrift', 'theme3', 'schrift_t1', 'schrift_t2', 'textschrift'] + \
        [f'wechsel{n}' for n in W] + [f'ziffer{n}' for n in Z] + [f'punkt{n}' for n in D]

laeufe = []
namen = [n for n in REIHE if os.path.exists(f'{HIER}/laeufe/{n}.jsonl')]
namen += sorted(os.path.basename(p)[:-6] for p in glob.glob(f'{HIER}/laeufe/*.jsonl')
                if os.path.basename(p)[:-6] not in namen and os.path.basename(p)[:-6] != 'probe')
for n in namen:
    bilder, ereignisse, ende = [], [], 0
    for z in open(f'{HIER}/laeufe/{n}.jsonl'):
        x = json.loads(z)
        if 'b' in x: bilder.append([x['t'], x['b']])
        elif 'ton' in x: ereignisse.append([x['t'], 'ton', x['ton']])
        elif 'uart' in x: ereignisse.append([x['t'], 'uart', x['uart']])
        elif 'led' in x: ereignisse.append([x['t'], 'led', x['led']])
        elif 'hell' in x: ereignisse.append([x['t'], 'hell', x['hell']])
        elif 'ende' in x: ende = x['t']
    skript = open(f'{HIER}/drehbuch/{n}.txt').read() if os.path.exists(f'{HIER}/drehbuch/{n}.txt') else ''
    titel, text = TITEL.get(n, (n, ''))
    laeufe.append({'id': n, 'titel': titel, 'text': text, 'gruppe': GRUPPE.get(n, 'More'), 'bilder': bilder,
                   'ereignisse': ereignisse, 'ende': ende, 'skript': skript})

seite = open(f'{HIER}/betrachter_vorlage.html').read().replace('/*DATEN*/[]', json.dumps(laeufe, ensure_ascii=False))
open(f'{HIER}/betrachter.html', 'w').write(seite)
print(f'betrachter.html: {len(laeufe)} runs')
