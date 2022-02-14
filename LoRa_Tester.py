import image, touch, gc, time
from machine import I2C
from board import board_info
from fpioa_manager import fm
from Maix import GPIO
import time
from machine import SPI
from micropython import const
from sx127x import SX127x

board_info=board_info()
i2c = I2C(I2C.I2C3, freq=1000*1000, scl=24, sda=27) # amigo
devices = i2c.scan()
print(devices)
touch.TouchLow.config(i2c)
tmp = touch.Touch(320, 480, 200)
whichButton = -1
message = "Welcome!"
rssi = ""
snr = ""
loraPacket = ""
myFreq = 433e6
mySF = 12
myBW = 7
myTX = 17
pingCounter = 0
squareWidth = 90
squareHeight = 70
check = [2,4,5,8]

################### config ###################
LORA_RST = const(22)
LORA_CS = const(12)
LORA_SPI_SCK = const(19)
LORA_SPI_MOSI = const(7)
LORA_SPI_MISO = const(9)
LORA_SPI_NUM = SPI.SPI1
LORA_SPI_FREQ_KHZ = const(100) 
##############################################

# gpio init
fm.register(LORA_RST, fm.fpioa.GPIOHS22, force=True) # RST
fm.register(LORA_CS, fm.fpioa.GPIOHS12, force=True) # CS
# set gpiohs work mode to output mode
cs = GPIO(GPIO.GPIOHS12, GPIO.OUT)
rst = GPIO(GPIO.GPIOHS22, GPIO.IN)

spi1 = SPI(LORA_SPI_NUM, mode=SPI.MODE_MASTER, baudrate=LORA_SPI_FREQ_KHZ * 1000, 
           polarity=0, phase=0, bits=8, firstbit=SPI.MSB, sck=LORA_SPI_SCK, 
           mosi=LORA_SPI_MOSI, miso = LORA_SPI_MISO)
lora = SX127x(spi=spi1, pin_ss=cs)

def Version():
    global message
    version = lora.readRegister(0x42)
    print("Version: 0x"+hex(version))
    message = "Version: "+hex(version)
    if version == 0x12:
        message += " [o]"
    else:
        message += " [x]"
    showMap()

def PING():
    global pingCounter, message
    payload = 'PING #{0}'.format(pingCounter)
    print("Sending packet: {}".format(payload))
    message = "Sent "+payload
    lora.print(payload)
    pingCounter += 1
    showMap()

def NOP():
     print("NOP")

def SF10():
    global mySF, check
    mySF = 10
    if check.count(4)>0: #SF12
        check.remove(4)
    if check.count(3)==0: #SF10
        check.append(3)
    setParameters()

def SF12():
    global mySF, check
    mySF = 12
    if check.count(3)>0: #SF10
        check.remove(3)
    if check.count(4)==0: #SF12
        check.append(4)
    setParameters()

def BW6():
    global myBW, check
    myBW = 6
    if check.count(2)>0: #BW7
        check.remove(2)
    if check.count(1)==0: #BW6
        check.append(1)
    setParameters()

def BW7():
    global myBW, check
    myBW = 7
    if check.count(1)>0: #BW6
        check.remove(1)
    if check.count(2)==0: #BW7
        check.append(2)
    setParameters()

def F433():
    global myFreq, check
    myFreq = 433e6
    if check.count(6)>0: #868
        check.remove(6)
    if check.count(5)==0: #433
        check.append(5)
    setParameters()

def F868():
    global myFreq, check
    myFreq = 868e6
    if check.count(5)>0: #433
        check.remove(5)
    if check.count(6)==0: #868
        check.append(6)
    setParameters()

def Tx10():
    global myTX, check
    myTX = 10
    if check.count(8)>0: #Tx17
        check.remove(8)
    if check.count(7)==0: #Tx10
        check.append(7)
    setParameters()

def Tx17():
    global myTX, check
    myTX = 17
    if check.count(7)>0: #Tx10
        check.remove(7)
    if check.count(8)==0: #Tx17
        check.append(8)
    setParameters()

menus = ["ping", "BW6", "BW7", "SF10", "SF12", "433", "868", "Tx10", "Tx17"]
actions = [PING, BW6, BW7, SF10, SF12, F433, F868, Tx10, Tx17]
numMenus = len(menus)

def setParameters():
    global mySF, myBW, myFreq, myTX, message
    # lora reset
    rst.value(0)
    time.sleep_ms(10)
    rst.value(1)
    time.sleep_ms(100)
    lora.init()
    fq = round(myFreq/1000000, 3)
    print("Setting freq to: {0} MHz".format(fq))
    lora.setFrequency(myFreq)
    bins = (7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3, 500E3)
    if myBW<0 or myBW>9:
        myBW=7
    BWrate = bins[myBW]
    print("Setting BW to: "+str(BWrate/1e3)+" KHz / "+str(myBW))
    lora.setSignalBandwidth(BWrate)
    print("Setting SF to: "+str(mySF))
    lora.setSpreadingFactor(mySF)
    print("Setting TX power to: "+str(myTX))
    lora.setTxPower(myTX)
    print("------------------------")
    print("Checking:")
    fq = round(lora.getFrequency()/1000000.0, 3)
    print("• fq: {0} MHz".format(fq))
    sf = lora.getSpreadingFactor()
    print("• sf: "+str(sf))
    bwnum, bw = lora.getSignalBandwidth()
    print("• bw: {0} ie {1} KHz".format(bwnum, (bw/1e3)))
    Pout, Pmax, paboost = lora.getTxPower()
    if paboost:
        paboost = "PA_BOOST pin"
    else:
        paboost = "RFO pin"
    print('Pout {0} dBm, Pmax {1}, {2}'.format(Pout, Pmax, paboost))
    print("------------------------")
    message = "{0} MHz SF{1} BW {2} KHz".format(fq, sf, round(bw/1e3, 1))
    showMap()

def showMap():
    global whichButton, message, loraPacket, rssi, snr, squareWidth, squareHeight
    img = image.Image(size=(320, 480))
    img.draw_rectangle(0, 0, 320, 480, color=(255, 64, 64), fill=True)
    img.draw_string(140, 10, "MENU", color=(255, 255, 255), scale=2)
    for i in range(0, numMenus):
        x = (i % 3) * (squareWidth+10) + 10
        y = int(i/3) * (squareHeight+10) + 50
        if whichButton == i:
            img.draw_rectangle(x, y, squareWidth, squareHeight, color=(0, 191, 191), fill=True)
        img.draw_rectangle(x, y, squareWidth, squareHeight, color=(0, 0, 0), thickness=3)
        clr = color=(255, 255, 255)
        if whichButton == i:
            clr = (33, 33, 33)
            offsetX = 32
            offsetY = 22
        if check.count(i)>0:
            # check mark
            img.draw_rectangle(x+3, y+3, squareWidth-6, squareHeight-6, color=(0, 0, 255), thickness=3)
        dsp = menus[i]
        offsetX = 45 - (8*len(dsp))
        img.draw_string(x+offsetX, y+20, dsp, clr, scale=3)
    py = y + squareHeight + 10
    ln = len(message)
    if ln > 0:
        myScale = 2
        myWidth = 5 * myScale
        img.draw_string(int((320-ln*myWidth)/2), 470-myScale*10, message, (0, 0, 0), scale=myScale)
    ln = len(loraPacket)
    if ln > 0:
        myScale = 2
        myWidth = 5 * myScale
        pieces=[]
        limit = 28
        while len(loraPacket)>0:
            pieces.append(loraPacket[0:limit])
            loraPacket=loraPacket[limit:]
        pieces.append(rssi+" "+snr)
        for i in pieces:
            ln = len(i)
            img.draw_string(6, py, i, (255, 222, 222), scale=myScale)
            py += 24
    lcd.rotation(1)
    lcd.mirror(1)
    lcd.display(img)
    gc.collect()

showMap()
setParameters()
while 1:
    tmp.event()
    #print(tmp.state, tmp.points)
    [(y0, x0, t0), (y1, x1, t1)] = tmp.points
    #print(str(x0)+":"+str(y0))
    if(x0!=0 and y0 != 0):
        print("Touch")
        while(x0!=0 and y0 != 0):
            saveX = x1
            saveY = y1
            if saveY<50:
                whichButton = -1
            else:
                x = int((saveX-10)/(squareWidth+10))
                y = int((saveY-50)/(squareHeight+10))
                whichButton = y*3+x
            showMap()
            tmp.event()
            [(y0, x0, t0), (y1, x1, t1)] = tmp.points
        print("Released")
        if saveY<50:
            print('abort')
        else:
            print(str(saveX)+":"+str(saveY))
            x = int((saveX-10)/(squareWidth+10))
            y = int((saveY-50)/(squareHeight+10))
            index = y*3+x
            if index>(numMenus-1):
                print('abort')
            else:
                print("You selected menu: "+str(index))
                actions[index]()
        whichButton = -1
        showMap()
    gc.collect()
    if lora.receivedPacket():
        try:
            loraPacket = lora.read_payload().decode()
            rssi = "RSSI: {}".format(lora.packetRssi())
            snr = "SNR: {}".format(lora.packetSNR())
            print("*** Received message *** {} {} {}".format(loraPacket, rssi, snr))
            message = "Incoming!"
            showMap()
        except Exception as e:
            print(e)
        gc.collect()
        time.sleep_ms(30)