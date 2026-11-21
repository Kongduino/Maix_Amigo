import time, wav
from lcd import LCD
lcd = LCD()
def say(msg, col):
    print('PHASE', msg)
    lcd.fill(col); lcd.text(msg, 40, 150, LCD.color(255, 255, 255)); lcd.show()
say('1: wav.play("/sd/hello.wav")', LCD.color(0, 120, 0))
t = time.ticks_ms(); wav.play('/sd/hello.wav', volume=90); print('played in', time.ticks_diff(time.ticks_ms(), t), 'ms')
for n in (3, 2, 1):
    say('speak when RED, in %d...' % n, LCD.color(80, 80, 80)); time.sleep(1)
say('2: RECORDING to /sd/note.wav (4 s)', LCD.color(170, 0, 0))
t = time.ticks_ms(); wav.record('/sd/note.wav', 4); print('recorded in', time.ticks_diff(time.ticks_ms(), t), 'ms')
import os; print('note.wav', os.stat('/sd/note.wav')[6], 'bytes')
say('3: wav.play("/sd/note.wav")', LCD.color(0, 0, 160))
wav.play('/sd/note.wav')
say('done', 0)
