import sensor, lcd, time

clock = time.clock()
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
sensor.skip_frames()

lcd.init(freq=15000000)

while(True):
    clock.tick()
    img = sensor.snapshot()
    fps =clock.fps()
    print("%2.1ffps" %(fps))
    lcd.display(img)
