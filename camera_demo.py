# Camera demo for MicroPython on the Maix Amigo: a live preview of the rear
# camera, upright on the screen (it follows the way the Amigo is held, in
# portrait). Tap the screen to switch to the front camera and back.
#
# Run it with: import camera_demo (or copy it to the board as main.py)
import gc, time
from lcd import LCD
from camera import Camera
from msa301 import MSA301
from touch import Touch
acc = MSA301()
white = LCD.color(255, 255, 255)
front = False
cam = Camera(front)
lcd, rot = None, 1
t, n, fps = time.ticks_ms(), 0, 0
while True:
    r = acc.rotation()
    if r in (1, 3) and r != rot or lcd is None:
        if r in (1, 3):
            rot = r
        lcd = touch = None
        gc.collect()
        lcd = LCD(rot)
        touch = Touch(rotation=rot)
    if touch.read():
        front = not front
        cam = None
        gc.collect()
        cam = Camera(front)
        while touch.read():
            pass
    frame = cam.capture()
    lcd.fill(0)
    lcd.blit(frame, (320 - cam.width) // 2, 40)
    n += 1
    if n % 10 == 0:
        fps = 10000 // max(time.ticks_diff(time.ticks_ms(), t), 1)
        t = time.ticks_ms()
    lcd.text('%s camera  %dx%d  %d fps' % ('front' if front else 'rear', cam.width, cam.height, fps), 8, 400, white)
    lcd.text('tap to switch', 8, 420, white)
    lcd.show()
