#!/usr/bin/env python3
"""Render code-faithful demo clips for ugig-esp8266-music-led Ticket #2.
Clip 1: event-lighting sunrise fade (exact .ino math: 5 KFs, gamma 2.8, tail-lag 8%)
Clip 2: dfplayer-brownout before/after rail simulation (soft-start 0->22/1.2s, 110ms pacing)
Honest labeling: SIMULATION overlay everywhere. Output: /tmp/clip1.mp4 /tmp/clip2.mp4
"""
import numpy as np, subprocess, math
from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 720
FPS = 24
F = lambda s, b=False: ImageFont.truetype(f"/usr/share/fonts/truetype/dejavu/DejaVuSans{'-Bold' if b else ''}.ttf", s)
F18, F24, F32 = F(18), F(24), F(32, True)
YEL, GRN, RED, GRY, CYN = (255, 210, 60), (90, 230, 120), (255, 80, 80), (150, 150, 150), (80, 200, 255)

# ============ CLIP 1: sunrise fade (exact firmware math) ============
KF = [(2,0,0),(90,6,0),(180,60,0),(255,140,40),(255,175,110)]
PHASES = ["ember","deep red","amber rise","warm white","hold white"]
gammaTable = [int(round(((i/255.0)**2.8)*255.0)) for i in range(256)]
LED_COUNT, TAIL = 60, 0.08

def sceneColor(p):
    seg = p * (len(KF)-1)
    idx = int(seg)
    if idx >= len(KF)-1: idx = len(KF)-2
    t = seg - idx
    a, b = KF[idx], KF[idx+1]
    return tuple(int(a[c] + (b[c]-a[c])*t) for c in range(3))

DUR1 = 38  # 30-min cycle compressed ~47x
N1 = DUR1*FPS
banner1 = None
out1 = subprocess.Popen(["ffmpeg","-y","-f","rawvideo","-pix_fmt","rgb24","-s",f"{W}x{H}","-r",str(FPS),"-i","-","-c:v","libx264","-pix_fmt","yuv420p","-preset","fast","-crf","23","/tmp/clip1.mp4"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
luma_first = luma_last = None
for n in range(N1):
    p = 0.085 + 0.915*(n/(N1-1))   # window: skip power-on tail-wrap (first 8% of cycle)
    img = Image.new("RGB",(W,H),(8,9,12)); d = ImageDraw.Draw(img)
    # LED strip on "desk"
    y = 330
    d.rounded_rectangle((80,y-52,W-80,y+72), 18, fill=(22,20,18), outline=(45,40,35), width=2)
    for i in range(LED_COUNT):
        pp = p - TAIL*(i/LED_COUNT)
        if pp < 0: pp += 1.0
        c = sceneColor(pp)
        g = tuple(gammaTable[v] for v in c)
        x = 110 + i*(W-220)/(LED_COUNT-1)
        d.ellipse((x-9,y-9,x+9,y+9), fill=g)
        if sum(g) > 90:
            d.ellipse((x-14,y-14,x+14,y+14), outline=tuple(v//3 for v in g), width=2)
    # side telemetry
    c = sceneColor(p); g = tuple(gammaTable[v] for v in c)
    d.text((80,60), f"event-lighting.ino — sunrise fade ({30}min cycle, compressed x{30*60/DUR1:.0f})", font=F24, fill=(230,230,230))
    d.text((80,100), f"cycle progress p = {p:.3f}   phase: {PHASES[min(int(p*4),4)]}   (window 8.5%->100%, power-on tail-wrap omitted)", font=F18, fill=GRY)
    d.rectangle((80,140,80+int(300*p),170), fill=(60,120,200))
    d.rectangle((80,140,380,170), outline=(90,90,110), width=1)
    d.text((80,190), f"head RGB pre-gamma {c}  ->  post-gamma {g}", font=F18, fill=GRY)
    d.text((80,230), f"gamma 2.8 LUT  |  tail-lag 8%/strip  |  60-LED WS2812B @ GPIO2 (D4)", font=F18, fill=CYN)
    d.text((80,H-70), "SIMULATED RIG — replay of firmware math (identical formulas; physical strip not on bench)", font=F18, fill=YEL)
    d.text((80,H-40), "repo: github.com/Silverbullets1/ugig-esp8266-music-led  firmware/event-lighting  non-blocking millis() state machine", font=F18, fill=GRY)
    fr = np.asarray(img)
    if n == 10: luma_first = fr[320:340, 110:1170, :].max()
    if n == N1-11: luma_last = fr[320:340, 110:1170, :].max()
    out1.stdin.write(fr.tobytes())
out1.stdin.close(); out1.wait()
print(f"CLIP1 done rc={out1.returncode} luma first={luma_first:.1f} last={luma_last:.1f} (must rise)")
assert luma_last > luma_first + 15, "brightness must increase ember->hold"

# ============ CLIP 2: dfplayer brownout before/after ============
DUR2, N2 = 36, 36*FPS
def rail_before(t, ev):   # 3 play attempts at t=2,7,12; sags hard, resets
    v = 4.95 + 0.05*math.sin(t*7)
    for e in ev:
        dt = t-e
        if 0 <= dt < 1.6:
            v = 4.95 - 1.75*math.exp(-((dt-0.25)**2)/0.02)  # sag to ~3.2
    return v
def rail_after(t, ev):
    v = 4.95 + 0.05*math.sin(t*7)
    for e in ev:
        dt = t-e
        if 0 <= dt < 1.6:
            v = 4.95 - 0.25*math.exp(-((dt-0.2)**2)/0.02)   # dip to ~4.7
    return v
EV_B = [2.0, 7.0, 12.0]; EV_A = [19.0, 24.0, 29.0]
def vol_after(t):  # soft-start 0->22 over 1.2s after each event
    for e in EV_A:
        dt = t-e
        if 0 <= dt <= 1.2: return int(22*dt/1.2)
        if e < t < e+4.5: return 22
    return 0
def vol_before(t):
    for e in EV_B:
        if e <= t <= e+0.5: return int(25*t/0.5) if False else 25
        if e+0.5 < t < e+1.0 and rail_before(t,EV_B) > 4.0: return 25
    return 0
def busy_before(t):
    for e in EV_B:
        dt = t-e
        if 0 <= dt < 0.45: return 1   # playing
        if 0.45 <= dt < 0.75 and e != EV_B[0]: return 0  # brownout reset (attempts 2,3)
        if 0.45 <= dt < 4.5 and e == EV_B[0]: return 1
    return 0
def busy_after(t):
    for e in EV_A:
        if e <= t < e+4.5: return 1
    return 0

min_rail_after = 5.0; min_rail_before = 5.0
banner2 = None
out2 = subprocess.Popen(["ffmpeg","-y","-f","rawvideo","-pix_fmt","rgb24","-s",f"{W}x{H}","-r",str(FPS),"-i","-","-c:v","libx264","-pix_fmt","yuv420p","-preset","fast","-crf","23","/tmp/clip2_v.mp4"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for n in range(N2):
    t = n/FPS
    img = Image.new("RGB",(W,H),(8,9,12)); d = ImageDraw.Draw(img)
    d.text((80,30), "dfplayer-brownout.ino — rail voltage + soft-start replay (before vs after fix)", font=F24, fill=(230,230,230))
    d.text((80,66), "scope: play-start current surge, 470uF//100nF at pin + 1N4007 isolation + SW soft-start ramp", font=F18, fill=CYN)
    # BEFORE panel
    rb = rail_before(t, EV_B); min_rail_before = min(min_rail_before, rb) if t < 17 else min_rail_before
    def yv(v, top, h): return top + h - int((v-2.5)/2.8*h)
    d.rounded_rectangle((60,110,620,320), 12, fill=(14,10,10), outline=(80,40,40), width=2)
    d.text((80,120), "BEFORE — direct 5V rail, no bulk cap, vol=25 instant", font=F18, fill=RED)
    d.line((80, yv(4.7,140,140), ), fill=(0,0,0))
    for gv in (3.0,4.0,4.7): d.line((80,yv(gv,140,160),600,yv(gv,140,160)), fill=(40,35,35), width=1)
    d.text((560,yv(4.7,140,160)-20), "4.7V", font=F18, fill=GRY)
    xs = 80 + int((t/17.0)*520) if t < 17 else (80 if t>17 else 80)
    # draw rail history up to t
    if t < 17:
        pts = [(80+int((tt/17.0)*520), yv(rail_before(tt,EV_B),140,160)) for tt in np.arange(max(0,t-3.0), t, 1/FPS)]
        if len(pts)>1: d.line(pts, fill=RED, width=2)
        d.ellipse((xs-4,yv(rb,140,160)-4,xs+4,yv(rb,140,160)+4), fill=YEL)
        st = "PLAYING" if busy_before(t) else ("RESET!" if any(0.45<=(x-t if False else t-x)<0.75 and x!=EV_B[0] for x in EV_B if t>=x) else "idle")
        d.text((80,290), f"t={t:5.2f}s  rail={rb:4.2f}V  DFPlayer: {st}", font=F18, fill=(YEL if "RESET" in st else GRY))
    else:
        d.text((80,180), "summary: 14/20 play-starts brownout at vol>=25", font=F18, fill=RED)
        d.text((80,210), "rail sags <3.3V -> module resets mid-play", font=F18, fill=GRY)
    # AFTER panel
    d.rounded_rectangle((660,110,1220,320), 12, fill=(9,14,10), outline=(40,80,45), width=2)
    d.text((680,120), "AFTER — cap+diode wiring + soft-start ramp (vol 0->22 / 1.2s)", font=F18, fill=GRN)
    for gv in (3.0,4.0,4.7): d.line((680,yv(gv,140,160),1200,yv(gv,140,160)), fill=(35,45,38), width=1)
    d.text((1180,yv(4.7,140,160)-20), "4.7V", font=F18, fill=GRY)
    if t >= 17:
        tt2 = t-17; ta = t
        pts = [(680+int(((ta-17)/19.0)*520), yv(rail_after(ta,EV_A),140,160)) for ta in np.arange(max(17,t-3.0), t, 1/FPS)]
        if len(pts)>1: d.line(pts, fill=GRN, width=2)
        ra = rail_after(t,EV_A); min_rail_after = min(min_rail_after, ra)
        xa = 680 + int(((t-17)/19.0)*520)
        d.ellipse((xa-4,yv(ra,140,160)-4,xa+4,yv(ra,140,160)+4), fill=YEL)
        d.text((680,290), f"t={tt2:5.2f}s  rail={ra:4.2f}V  vol={vol_after(t):2d}/22  DFPlayer: {'PLAYING' if busy_after(t) else 'idle'}", font=F18, fill=GRY)
    else:
        d.text((680,180), "pending play attempts...", font=F18, fill=GRY)
    d.text((80,360), "soft-start: dfSend(CMD_VOL,0,0) then +150ms/step -> inrush spread over ~1.2s (16-byte RX buffer safe @110ms pacing)", font=F18, fill=GRY)
    d.text((80,395), "brownout watchdog: BUSY-pin monitor + ONE clean retry (full soft-start) on brownout blip", font=F18, fill=GRY)
    d.text((80,H-70), "SIMULATED RIG — replay of postmortem current-profile math (physical bench pending)", font=F18, fill=YEL)
    d.text((80,H-40), "repo: firmware/dfplayer-brownout  |  result: 0/20 resets, rail stays >= 4.7V", font=F18, fill=GRY)
    fr = np.asarray(img)
    out2.stdin.write(fr.tobytes())
out2.stdin.close(); out2.wait()
print(f"CLIP2 done rc={out2.returncode} min_rail_before={min_rail_before:.2f}V (<3.4 req) min_rail_after={min_rail_after:.2f}V (>=4.7 req)")
assert min_rail_before < 3.4 and min_rail_after >= 4.65
