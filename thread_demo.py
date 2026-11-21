# _thread demo for MicroPython on the Maix Amigo: a thread on the K210's
# second core blinks the red LED while the main code, on the first core,
# computes; the two run at the same time.
#
# There is no global lock between the cores (as on the Raspberry Pi Pico),
# so this follows the rule that keeps that safe:
#
#   - The main code is in a function (main()), not at the top level of the
#     script: there, every new variable is a new global, and a dict being
#     resized for a new key can miss keys for a moment, so the thread,
#     looking up a global (time, led...) just then, can get a NameError.
#   - The thread is given what it uses as arguments (led, state, lock)
#     instead of reading globals.
#   - What both cores change (state) is only touched with the lock held.
#
# Run it with: import thread_demo
import _thread
import time
from machine import Pin


def blink(led, state, lock):
    # On core 1: blinks until main() says to stop
    while True:
        with lock:
            if not state["run"]:
                break
            state["blinks"] += 1
        led(0)  # the LEDs are lit when their pin is low
        time.sleep_ms(100)
        led(1)
        time.sleep_ms(100)
    with lock:
        state["done"] = True


def main():
    # On core 0
    led = Pin(14, Pin.OUT, value=1)  # red
    state = {"run": True, "blinks": 0, "done": False}
    lock = _thread.allocate_lock()
    _thread.start_new_thread(blink, (led, state, lock))

    # Meanwhile, some work on this core
    t = time.ticks_ms()
    total = 0
    for i in range(300000):
        total += i * i
    print("core 0 computed", total, "in", time.ticks_diff(time.ticks_ms(), t), "ms")
    time.sleep(2)

    # Tell the thread to stop, and wait for it
    with lock:
        state["run"] = False
    while True:
        with lock:
            if state["done"]:
                break
        time.sleep_ms(10)
    print("core 1 blinked", state["blinks"], "times meanwhile")


main()
