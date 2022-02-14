# DATE: 2020-12-3
import gc
import machine

PA_OUTPUT_RFO_PIN = 0
PA_OUTPUT_PA_BOOST_PIN = 1

# registers
REG_FIFO = 0x00
REG_OP_MODE = 0x01
REG_FRF_MSB = 0x06
REG_FRF_MID = 0x07
REG_FRF_LSB = 0x08
REG_PA_CONFIG = 0x09
REG_LNA = 0x0c
REG_FIFO_ADDR_PTR = 0x0d

REG_FIFO_TX_BASE_ADDR = 0x0e
FifoTxBaseAddr = 0x00
# FifoTxBaseAddr = 0x80

REG_FIFO_RX_BASE_ADDR = 0x0f
FifoRxBaseAddr = 0x00
REG_FIFO_RX_CURRENT_ADDR = 0x10
REG_IRQ_FLAGS_MASK = 0x11
REG_IRQ_FLAGS = 0x12
REG_RX_NB_BYTES = 0x13
REG_PKT_SNR_VALUE = 0x19
REG_PKT_RSSI_VALUE = 0x1a
REG_PKT_SNR_VALUE = 0x1b
REG_MODEM_CONFIG_1 = 0x1d
REG_MODEM_CONFIG_2 = 0x1e
REG_PREAMBLE_MSB = 0x20
REG_PREAMBLE_LSB = 0x21
REG_PAYLOAD_LENGTH = 0x22
REG_FIFO_RX_BYTE_ADDR = 0x25
REG_MODEM_CONFIG_3 = 0x26
REG_RSSI_WIDEBAND = 0x2c
REG_DETECTION_OPTIMIZE = 0x31
REG_DETECTION_THRESHOLD = 0x37
REG_SYNC_WORD = 0x39
REG_DIO_MAPPING_1 = 0x40
REG_VERSION = 0x42

# modes
MODE_LONG_RANGE_MODE = 0x80  # bit 7: 1 => LoRa mode
MODE_SLEEP = 0x00
MODE_STDBY = 0x01
MODE_TX = 0x03
MODE_RX_CONTINUOUS = 0x05
MODE_RX_SINGLE = 0x06

# PA config
PA_BOOST = 0x80
MAX_POWER = 0x70

# IRQ masks
IRQ_TX_DONE_MASK = 0x08
IRQ_PAYLOAD_CRC_ERROR_MASK = 0x20
IRQ_RX_DONE_MASK = 0x40
IRQ_RX_TIME_OUT_MASK = 0x80

# Buffer size
MAX_PKT_LENGTH = 255

def twos(val): # 8-bit
    if (val & (1 << 7)) != 0:
        val = val - (1 << 8)
    return (val & 0xff)

class SX127x:
    # The controller can be ESP8266, ESP32, Raspberry Pi, or a PC.
    # The controller needs to provide an interface consisted of:
    # 1. a SPI, with transfer function.
    # 2. a reset pin, with low(), high() functions.
    # 3. IRQ pinS , to be triggered by RFM96W's DIO0~5 pins. These pins each has two functions:
    #   3.1 set_handler_for_irq_on_rising_edge()
    #   3.2 detach_irq()
    # 4. a function to blink on-board LED.

    def __init__(self,
                 spi, pin_ss, name = 'SX127x',
                 parameters = {'frequency' : 433E6, 'tx_power_level': 20, 'signal_bandwidth': 125E3,
                               'spreading_factor': 10, 'coding_rate': 5, 'preamble_length': 8,
                               'implicitHeader'  : False, 'sync_word': 0x12, 'enable_CRC': False},
                 onReceive = None):
        self.name = name
        self.parameters = parameters
        self._onReceive = onReceive
        self._lock = False
        self.spi = spi
        self.pin_ss = pin_ss

    def init(self, parameters = None):
        if parameters:
            self.parameters = parameters
        # check version
        version = self.readRegister(REG_VERSION)
        if version != 0x12:
            raise Exception('Invalid version: ', version)
        else:
            print('Version: 0x12')
        # put in LoRa and sleep mode
        self.sleep()
        # config
        self.setFrequency(self.parameters['frequency'])
        self.setSignalBandwidth(self.parameters['signal_bandwidth'])
        # set LNA boost
        self.writeRegister(REG_LNA, self.readRegister(REG_LNA) | 0x03)
        # set auto AGC
        self.writeRegister(REG_MODEM_CONFIG_3, 0x04)
        self.setTxPower(self.parameters['tx_power_level'])
        self._implicitHeaderMode = None
        self.implicitHeaderMode(self.parameters['implicitHeader'])
        self.setSpreadingFactor(self.parameters['spreading_factor'])
        self.setCodingRate(self.parameters['coding_rate'])
        self.setPreambleLength(self.parameters['preamble_length'])
        self.setSyncWord(self.parameters['sync_word'])
        self.enableCRC(self.parameters['enable_CRC'])
        # set LowDataRateOptimize flag if symbol time > 16ms (default disable on reset)
        # self.writeRegister(REG_MODEM_CONFIG_3, self.readRegister(REG_MODEM_CONFIG_3) & 0xF7)  # default disable on reset
        if 1000 / (self.parameters['signal_bandwidth'] / 2 ** self.parameters['spreading_factor']) > 16:
            self.writeRegister(REG_MODEM_CONFIG_3, self.readRegister(REG_MODEM_CONFIG_3) | 0x08)
        # set base addresses
        self.writeRegister(REG_FIFO_TX_BASE_ADDR, FifoTxBaseAddr)
        self.writeRegister(REG_FIFO_RX_BASE_ADDR, FifoRxBaseAddr)
        self.standby()

    def beginPacket(self, implicitHeaderMode = False):
        self.standby()
        self.implicitHeaderMode(implicitHeaderMode)
        # reset FIFO address and paload length
        self.writeRegister(REG_FIFO_ADDR_PTR, FifoTxBaseAddr)
        self.writeRegister(REG_PAYLOAD_LENGTH, 0)

    def endPacket(self):
        # put in TX mode
        self.writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX)
        # wait for TX done, standby automatically on TX_DONE
        while (self.readRegister(REG_IRQ_FLAGS) & IRQ_TX_DONE_MASK) == 0:
            pass
        # clear IRQs
        self.writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK)
        self.collect_garbage()

    def write(self, buffer):
        currentLength = self.readRegister(REG_PAYLOAD_LENGTH)
        size = len(buffer)
        # check size
        size = min(size, (MAX_PKT_LENGTH - FifoTxBaseAddr - currentLength))
        # write data
        for i in range(size):
            self.writeRegister(REG_FIFO, buffer[i])
        # update length
        self.writeRegister(REG_PAYLOAD_LENGTH, currentLength + size)
        return size

    def aquire_lock(self, lock = False):
        if 0:  # MicroPython is single threaded, doesn't need lock.
            if lock:
                while self._lock:
                    pass
                self._lock = True
            else:
                self._lock = False

    def print(self, string, implicitHeader = False):
        self.aquire_lock(True)  # wait until RX_Done, lock and begin writing.
        self.beginPacket(implicitHeader)
        self.write(string.encode())
        self.endPacket()
        self.aquire_lock(False) # unlock when done writing

    def getIrqFlags(self):
        irqFlags = self.readRegister(REG_IRQ_FLAGS)
        self.writeRegister(REG_IRQ_FLAGS, irqFlags)
        return irqFlags

    def packetRssi(self):
        return (self.readRegister(REG_PKT_RSSI_VALUE) - (164 if self._frequency < 868E6 else 157))
    
    def packetSNR(self):
        return (self.readRegister(REG_PKT_SNR_VALUE)) * 0.25

    def standby(self):
        self.writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY)

    def sleep(self):
        self.writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP)

    def setTxPower(self, level, outputPin = PA_OUTPUT_PA_BOOST_PIN):
        if (outputPin == PA_OUTPUT_RFO_PIN):
            # RFO
            level = min(max(level, 0), 14)
            self.writeRegister(REG_PA_CONFIG, 0x70 | level)
        else:
            # PA BOOST
            level = min(max(level, 2), 17)
            self.writeRegister(REG_PA_CONFIG, PA_BOOST | MAX_POWER | (level - 2))

    def getTxPower(self):
        regpa = self.readRegister(REG_PA_CONFIG)
        paboost = (regpa & 0x80 > 0)
        maxpower = (regpa & 0b01110000) >> 4
        OutputPower = regpa & 0b00001111
        Pmax=10.8+0.6*maxpower
        if paboost:
            Pout=17-(15-OutputPower)
        else:
            Pout=Pmax-(15-OutputPower)
        return [Pout, Pmax, paboost]

    def setFrequency(self, frequency):
        self._frequency = frequency
        #frfs = {169E6: (42, 64, 0),
        #        433E6: (108, 64, 0),
        #        434E6: (108, 128, 0),
        #        866E6: (216, 128, 0),
        #        868E6: (217, 0, 0),
        #        915E6: (228, 192, 0)}
        # that's stupid and lazy.
        # Enable all and any frequency
        #FXOSC = 32000000.0
        #FSTEP = (FXOSC / 524288)
        # 61.03516
        frf = int(frequency / 61.03516)
        self.writeRegister(REG_FRF_MSB, (frf >> 16) & 0xff)
        self.writeRegister(REG_FRF_MID, (frf >> 8) & 0xff)
        self.writeRegister(REG_FRF_LSB, frf & 0xff)

    def getFrequency(self):
        lsb = self.readRegister(REG_FRF_LSB)
        mid = self.readRegister(REG_FRF_MID)
        msb = self.readRegister(REG_FRF_MSB)
        frf = lsb + (mid << 8) + (msb <<16)
        return int(frf * 61.03516)

    def setSpreadingFactor(self, sf):
        sf = min(max(sf, 6), 12)
        self.writeRegister(REG_DETECTION_OPTIMIZE, 0xc5 if sf == 6 else 0xc3)
        self.writeRegister(REG_DETECTION_THRESHOLD, 0x0c if sf == 6 else 0x0a)
        self.writeRegister(REG_MODEM_CONFIG_2, (self.readRegister(REG_MODEM_CONFIG_2) & 0x0f) | ((sf << 4) & 0xf0))

    def getSpreadingFactor(self):
        cf2 = self.readRegister(REG_MODEM_CONFIG_2)
        return ((cf2 >> 4) & 0x0f)

    def setSignalBandwidth(self, sbw):
        sbw = abs(sbw) # just in case there's an idiot in the room
        bins = (7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3, 500E3)
        # Added 500KHz
        # Enable setting BW by frequency or numbers
        # setSignalBandwidth(6) --> 62.5E3
        if sbw <10:
            bw =  bins[sbw]
            print(bw)
        else:
            for i in range(len(bins)):
                if sbw <= bins[i]:
                    bw = i
                    break
        self.writeRegister(REG_MODEM_CONFIG_1, int((self.readRegister(REG_MODEM_CONFIG_1) & 0x0f) | (int(bw) << 4)))

    def getSignalBandwidth(self):
        cf1 = self.readRegister(REG_MODEM_CONFIG_1)
        bw = (cf1 >> 4) & 0x0f
        bins = (7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3)
        return [bw, bins[bw]]

    def setCodingRate(self, denominator):
        denominator = min(max(denominator, 5), 8)
        cr = denominator - 4
        self.writeRegister(REG_MODEM_CONFIG_1, (self.readRegister(REG_MODEM_CONFIG_1) & 0xf1) | (cr << 1))

    def getCodingRate(self):
        cf1 = self.readRegister(REG_MODEM_CONFIG_1)
        cr = (cf1 & 0x0e) >> 1
        return cr+4

    def setPreambleLength(self, length):
        self.writeRegister(REG_PREAMBLE_MSB, (length >> 8) & 0xff)
        self.writeRegister(REG_PREAMBLE_LSB, (length >> 0) & 0xff)

    def getPreambleLength(self):
        msb=self.readRegister(REG_PREAMBLE_MSB)
        lsb=self.readRegister(REG_PREAMBLE_LSB)
        return (msb<<8) | lsb

    def enableCRC(self, enable_CRC = False):
        modem_config_2 = self.readRegister(REG_MODEM_CONFIG_2)
        config = modem_config_2 | 0x04 if enable_CRC else modem_config_2 & 0xfb
        self.writeRegister(REG_MODEM_CONFIG_2, config)

    def setSyncWord(self, sw):
        self.writeRegister(REG_SYNC_WORD, sw)

    def getSyncWord(self):
        return self.readRegister(REG_SYNC_WORD)

    # def enable_Rx_Done_IRQ(self, enable = True):
    #     if enable:
    #         self.writeRegister(REG_IRQ_FLAGS_MASK, self.readRegister(REG_IRQ_FLAGS_MASK) & ~IRQ_RX_DONE_MASK)
    #     else:
    #         self.writeRegister(REG_IRQ_FLAGS_MASK, self.readRegister(REG_IRQ_FLAGS_MASK) | IRQ_RX_DONE_MASK)
    #
    # def dumpRegisters(self):
    #     for i in range(128):
    #         print("0x{0:02x}: {1:02x}".format(i, self.readRegister(i)))

    def implicitHeaderMode(self, implicitHeaderMode = False):
        if self._implicitHeaderMode != implicitHeaderMode:  # set value only if different.
            self._implicitHeaderMode = implicitHeaderMode
            modem_config_1 = self.readRegister(REG_MODEM_CONFIG_1)
            config = modem_config_1 | 0x01 if implicitHeaderMode else modem_config_1 & 0xfe
            self.writeRegister(REG_MODEM_CONFIG_1, config)

    def onReceive(self, callback):
        self._onReceive = callback
        if self.pin_RxDone:
            if callback:
                self.writeRegister(REG_DIO_MAPPING_1, 0x00)
                self.pin_RxDone.set_handler_for_irq_on_rising_edge(handler = self.handleOnReceive)
            else:
                self.pin_RxDone.detach_irq()

    def receive(self, size = 0):
        self.implicitHeaderMode(size > 0)
        if size > 0:
            self.writeRegister(REG_PAYLOAD_LENGTH, size & 0xff)
        # The last packet always starts at FIFO_RX_CURRENT_ADDR
        # no need to reset FIFO_ADDR_PTR
        self.writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS)

    # on RPi, interrupt callback is threaded and racing with main thread,
    # Needs a lock for accessing FIFO.
    # https://sourceforge.net/p/raspberry-gpio-python/wiki/Inputs/
    # http://raspi.tv/2013/how-to-use-interrupts-with-python-on-the-raspberry-pi-and-rpi-gpio-part-2
    def handleOnReceive(self, event_source):
        self.aquire_lock(True)  # lock until TX_Done
        # irqFlags = self.getIrqFlags() should be 0x50
        if (self.getIrqFlags() & IRQ_PAYLOAD_CRC_ERROR_MASK) == 0:
            if self._onReceive:
                payload = self.read_payload()
                self.aquire_lock(False)  # unlock when done reading
                self._onReceive(self, payload)
        self.aquire_lock(False)  # unlock in any case.

    def receivedPacket(self, size = 0):
        irqFlags = self.getIrqFlags()
        self.implicitHeaderMode(size > 0)
        if size > 0:
            self.writeRegister(REG_PAYLOAD_LENGTH, size & 0xff)
        # if (irqFlags & IRQ_RX_DONE_MASK) and \
        #         (irqFlags & IRQ_RX_TIME_OUT_MASK == 0) and \
        #         (irqFlags & IRQ_PAYLOAD_CRC_ERROR_MASK == 0):
        if (irqFlags == IRQ_RX_DONE_MASK):  # RX_DONE only, irqFlags should be 0x40
            # automatically standby when RX_DONE
            return True
        elif self.readRegister(REG_OP_MODE) != (MODE_LONG_RANGE_MODE | MODE_RX_SINGLE):
            # no packet received.
            # reset FIFO address / # enter single RX mode
            self.writeRegister(REG_FIFO_ADDR_PTR, FifoRxBaseAddr)
            self.writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_SINGLE)

    def read_payload(self):
        # set FIFO address to current RX address
        # fifo_rx_current_addr = self.readRegister(REG_FIFO_RX_CURRENT_ADDR)
        self.writeRegister(REG_FIFO_ADDR_PTR, self.readRegister(REG_FIFO_RX_CURRENT_ADDR))
        # read packet length
        packetLength = self.readRegister(REG_PAYLOAD_LENGTH) if self._implicitHeaderMode else \
            self.readRegister(REG_RX_NB_BYTES)
        payload = bytearray()
        for i in range(packetLength):
            payload.append(self.readRegister(REG_FIFO))
        self.collect_garbage()
        return bytes(payload)

    def readRegister(self, address, byteorder = 'big', signed = False):
        response = self.transfer(self.pin_ss, address & 0x7f)
        return int.from_bytes(response, byteorder)

    def writeRegister(self, address, value):
        self.transfer(self.pin_ss, address | 0x80, value)

    def transfer(self, cs, address, value = None):
        ret = 0
        cs.value(0)
        self.spi.write(address)
        if value:
            self.spi.write(value)
        else:
            ret = self.spi.read(1)
        cs.value(1)
        return ret

    def collect_garbage(self):
        gc.collect()
        # if config_lora.IS_MICROPYTHON:
        if 1:
            print('[Memory - free: {}   allocated: {}]'.format(gc.mem_free(), gc.mem_alloc()))
