# QR code scanner demo for MicroPython on the Maix Amigo: the rear camera's
# picture, upright on the screen (it follows the way the Amigo is held, in
# portrait), with a code it reads outlined in green, its text below, and a
# beep on the speaker for each new one.
#
# Run it with: import qr_demo (or copy it to the board as main.py)
import gc, math, struct, time
from machine import I2S
from lcd import LCD
from camera import Camera
from msa301 import MSA301
from es8374 import ES8374

X, Y = 40, 40  # where the camera's 240x320 picture goes
acc = MSA301()
cam = Camera()
white, green, grey = LCD.color(255, 255, 255), LCD.color(0, 255, 0), LCD.color(140, 140, 140)

codec = ES8374()
codec.volume(70)
beep = bytearray(2 * 1600)  # 0.1 s of 2 kHz at 16 kHz
for i in range(1600):
    struct.pack_into('<h', beep, 2 * i, int(8000 * math.sin(2 * math.pi * i / 8)))


def play_beep():
    out = I2S(0, mode=I2S.TX, rate=16000, bits=16, format=I2S.MONO, ibuf=4096, **ES8374.TX_PINS)
    out.write(beep)
    out.write(bytearray(4096))
    out.deinit()


def wrap(text, width=38):
    lines = []
    for para in text.split('\n'):
        while len(para) > width:
            lines.append(para[:width])
            para = para[width:]
        lines.append(para)
    return lines


lcd, rot = None, 1
last, outline, shown_at = None, None, 0
while True:
    r = acc.rotation()
    if r in (1, 3) and r != rot or lcd is None:
        if r in (1, 3):
            rot = r
        lcd = None
        gc.collect()
        lcd = LCD(rot)
    found = cam.qr(corners=True)
    frame = cam.capture()
    lcd.fill(0)
    lcd.blit(frame, X, Y)
    if found:
        payload, outline = found[0]
        shown_at = time.ticks_ms()
        if payload != last:
            last = payload
            print(payload)
            play_beep()
    elif outline and time.ticks_diff(time.ticks_ms(), shown_at) > 500:
        outline = None
    if outline:
        for k in range(4):
            (x0, y0), (x1, y1) = outline[k], outline[(k + 1) % 4]
            for d in (-1, 0, 1):  # 3 pixels wide
                lcd.line(X + x0 + d, Y + y0, X + x1 + d, Y + y1, green)
                lcd.line(X + x0, Y + y0 + d, X + x1, Y + y1 + d, green)
    if last is None:
        lcd.text('point the camera at a QR code', 40, 380, grey)
    else:
        try:
            text = last.decode()
        except UnicodeError:
            text = repr(last)
        for i, line in enumerate(wrap(text)[:9]):
            lcd.text(line, 8, 376 + 11 * i, white if outline else grey)
    lcd.show()
