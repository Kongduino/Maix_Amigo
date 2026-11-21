# Arrow demo for MicroPython on the Maix Amigo: an arrow that always points
# up, however the Amigo is held (the accelerometer turns the screen), and
# changes to a random colour when tapped.
#
# Copy it to the Amigo as main.py to run it at start-up.

from lcd import LCD
from touch import Touch
from msa301 import MSA301
from axp173 import AXP173
import gc, random, time

acc = MSA301()
pmu = AXP173()
white, grey = LCD.color(255, 255, 255), LCD.color(140, 140, 140)
arrow_color = LCD.color(255, 220, 0)
lcd = None
touch = None
rot = 0

HEAD_TOP = 40  # the arrow's head: rows 40 to 99, as wide as its depth
HEAD_ROWS = 60
SHAFT_TOP = 100  # its shaft: 41 pixels wide, rows 100 to 189
SHAFT_HEIGHT = 90
SHAFT_HALF = 20
MARGIN = 10  # a tap this close still counts


def random_color():
    # A bright, saturated colour: one channel full, one low, one anywhere
    channels = [255, random.getrandbits(6), random.getrandbits(8)]
    for i in range(2, 0, -1):  # shuffle
        j = random.getrandbits(8) % (i + 1)
        channels[i], channels[j] = channels[j], channels[i]
    return LCD.color(*channels)


def draw_arrow():
    cx = lcd.width // 2
    for i in range(HEAD_ROWS):
        lcd.hline(cx - i, HEAD_TOP + i, 2 * i + 1, arrow_color)
    lcd.fill_rect(cx - SHAFT_HALF, SHAFT_TOP, 2 * SHAFT_HALF + 1, SHAFT_HEIGHT, arrow_color)


def draw_battery():
    cx = lcd.width // 2
    lcd.fill_rect(cx - 72, 250, 160, 8, 0)
    lcd.text("battery %d mV %d%%" % (pmu.battery_voltage(), pmu.battery_level() or 0), cx - 72, 250, grey)


def draw():
    W, H = lcd.width, lcd.height
    lcd.fill(0)
    lcd.rect(0, 0, W, H, grey)
    draw_arrow()
    cx = W // 2
    lcd.text("tap the arrow", cx - 52, 210, white)
    lcd.text("rotation %d  (%dx%d)" % (rot, W, H), cx - 72, 230, white)
    draw_battery()


def on_arrow(x, y):
    dx = abs(x - lcd.width // 2)
    if HEAD_TOP - MARGIN <= y < HEAD_TOP + HEAD_ROWS:
        return dx <= max(y - HEAD_TOP, 0) + MARGIN
    return SHAFT_TOP <= y < SHAFT_TOP + SHAFT_HEIGHT + MARGIN and dx <= SHAFT_HALF + MARGIN


touching = False
last_battery = time.ticks_ms()
while True:
    r = acc.rotation()
    if lcd is None or (r is not None and r != rot):
        if r is not None:
            rot = r
        lcd = None
        gc.collect()
        lcd = LCD(rot)
        touch = Touch(rotation=rot)
        draw()
        lcd.show()

    p = touch.read()
    if p and not touching and on_arrow(*p):
        # a new touch, on the arrow
        arrow_color = random_color()
        draw_arrow()
        lcd.show()
    touching = p is not None

    if time.ticks_diff(time.ticks_ms(), last_battery) > 10000:
        last_battery = time.ticks_ms()
        draw_battery()
        lcd.show()
    time.sleep_ms(20)
