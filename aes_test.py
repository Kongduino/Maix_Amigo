import maes
from hexdump import hexDump
import time, random

randomBuff = bytearray(112)
for i in range(0, 112):
    randomBuff[i] = random.randint(0, 255)

#pKey = randomBuff[0:32]
#pIV = randomBuff[32:48]
pKey = randomBuff[0:16]
pIV = randomBuff[16:32]
#plaintext = bytes("this is a random sentence...", 'ascii')
#plaintext = bytearray(plaintext.encode())
#plaintext = randomBuff[48:112]
plaintext = randomBuff[48:109]
if len(plaintext) % 16 != 0:
    n = 16 - (len(plaintext) % 16)
    n = n.to_bytes(1, 'big')
    while len(plaintext) % 16 != 0:
        plaintext =  plaintext + n
print("> Key")
hexDump(pKey)
print("> IV")
hexDump(pIV)
print("> Plaintext")
hexDump(plaintext)

goal = time.ticks_ms() + 1000
count = 0
while time.ticks_ms() < goal:
    cryptor = maes.new(pKey, maes.MODE_CBC, IV=pIV)
    ciphertext = cryptor.encrypt(plaintext)
    count += 1
print("Encryption: {} rounds per second".format(count))
maes_enc_count = count

goal = time.ticks_ms() + 1000
count = 0
while time.ticks_ms() < goal:
    decryptor = maes.new(pKey, maes.MODE_CBC, IV=pIV)
    decrypted = decryptor.decrypt(ciphertext)
    count += 1
print("Decryption: {} rounds per second".format(count))
maes_dec_count = count

print("> Ciphertext")
hexDump(ciphertext)
print("> Deciphered")
hexDump(decrypted)

if decrypted[0:64] == plaintext[0:64]:
    print("[√] Test successful!")
else:
    print("[x] Test failed!")

# The same, with cryptolib: AES in C, built into the firmware. An aes object
# does either encryption or decryption, so a new one is made for each, as
# with maes.
try:
    import cryptolib
except ImportError:
    cryptolib = None
    print("No cryptolib in this firmware")

if cryptolib:
    print()
    print("--- cryptolib (C) ---")
    goal = time.ticks_ms() + 1000
    count = 0
    while time.ticks_ms() < goal:
        cryptor = cryptolib.aes(pKey, cryptolib.MODE_CBC, pIV)
        c_ciphertext = cryptor.encrypt(plaintext)
        count += 1
    c_enc = count
    print("Encryption: {} rounds per second".format(count))

    goal = time.ticks_ms() + 1000
    count = 0
    while time.ticks_ms() < goal:
        decryptor = cryptolib.aes(pKey, cryptolib.MODE_CBC, pIV)
        c_decrypted = decryptor.decrypt(c_ciphertext)
        count += 1
    c_dec = count
    print("Decryption: {} rounds per second".format(count))

    # (maes returns an array, which never equals bytes in MicroPython)
    if c_ciphertext == bytes(ciphertext):
        print("[√] Same ciphertext as maes")
    else:
        print("[x] Ciphertext differs from maes!")
        hexDump(c_ciphertext)
    if c_decrypted == plaintext:
        print("[√] cryptolib test successful!")
    else:
        print("[x] cryptolib test failed!")
    print("cryptolib is {:.0f}x faster encrypting, {:.0f}x faster decrypting".format(
        c_enc / maes_enc_count, c_dec / maes_dec_count))

    # Throughput on a larger buffer: 4 KB, encrypted and decrypted in one call
    big = bytes(randomBuff[i % 112] for i in range(4096))
    t = time.ticks_us()
    big_ct = cryptolib.aes(pKey, cryptolib.MODE_CBC, pIV).encrypt(big)
    t_enc = time.ticks_diff(time.ticks_us(), t)
    t = time.ticks_us()
    big_pt = cryptolib.aes(pKey, cryptolib.MODE_CBC, pIV).decrypt(big_ct)
    t_dec = time.ticks_diff(time.ticks_us(), t)
    print("4 KB: encrypted in {} us ({} KB/s), decrypted in {} us ({} KB/s), {}".format(
        t_enc, 4096 * 1000 // t_enc, t_dec, 4096 * 1000 // t_dec,
        "round trip OK" if big_pt == big else "round trip FAILED"))

# The same again with the K210's AES accelerator (k210.AES, the same calls as
# cryptolib.aes)
try:
    from k210 import AES as HwAES
except ImportError:
    HwAES = None
    print("No k210.AES in this firmware")

if HwAES:
    print()
    print("--- k210.AES (hardware) ---")
    goal = time.ticks_ms() + 1000
    count = 0
    while time.ticks_ms() < goal:
        cryptor = HwAES(pKey, HwAES.MODE_CBC, pIV)
        h_ciphertext = cryptor.encrypt(plaintext)
        count += 1
    h_enc = count
    print("Encryption: {} rounds per second".format(count))

    goal = time.ticks_ms() + 1000
    count = 0
    while time.ticks_ms() < goal:
        decryptor = HwAES(pKey, HwAES.MODE_CBC, pIV)
        h_decrypted = decryptor.decrypt(h_ciphertext)
        count += 1
    h_dec = count
    print("Decryption: {} rounds per second".format(count))

    if h_ciphertext == bytes(ciphertext):
        print("[√] Same ciphertext as maes")
    else:
        print("[x] Ciphertext differs from maes!")
        hexDump(h_ciphertext)
    if h_decrypted == plaintext:
        print("[√] k210.AES test successful!")
    else:
        print("[x] k210.AES test failed!")
    print("k210.AES is {:.0f}x faster encrypting, {:.0f}x faster decrypting than maes".format(
        h_enc / maes_enc_count, h_dec / maes_dec_count))

    big = bytes(randomBuff[i % 112] for i in range(4096))
    t = time.ticks_us()
    big_ct = HwAES(pKey, HwAES.MODE_CBC, pIV).encrypt(big)
    t_enc = time.ticks_diff(time.ticks_us(), t)
    t = time.ticks_us()
    big_pt = HwAES(pKey, HwAES.MODE_CBC, pIV).decrypt(big_ct)
    t_dec = time.ticks_diff(time.ticks_us(), t)
    print("4 KB: encrypted in {} us ({} KB/s), decrypted in {} us ({} KB/s), {}".format(
        t_enc, 4096 * 1000 // t_enc, t_dec, 4096 * 1000 // t_dec,
        "round trip OK" if big_pt == big else "round trip FAILED"))

    # GCM: encryption and authentication in one pass (hardware only; maes and
    # cryptolib don't have it). A GCM object does one job, with a 12-byte nonce.
    print()
    print("--- k210.AES GCM (hardware) ---")
    nonce = randomBuff[32:44]
    aad = b"header, authenticated but not encrypted"
    goal = time.ticks_ms() + 1000
    count = 0
    while time.ticks_ms() < goal:
        g_ciphertext, tag = HwAES(pKey, HwAES.MODE_GCM, nonce).encrypt_and_digest(plaintext, aad)
        count += 1
    print("Encryption + tag: {} rounds per second".format(count))
    goal = time.ticks_ms() + 1000
    count = 0
    while time.ticks_ms() < goal:
        g_decrypted = HwAES(pKey, HwAES.MODE_GCM, nonce).decrypt_and_verify(g_ciphertext, tag, aad)
        count += 1
    print("Decryption + check: {} rounds per second".format(count))
    print("> Tag")
    hexDump(tag)
    if g_decrypted == plaintext:
        print("[√] GCM test successful!")
    else:
        print("[x] GCM test failed!")
    tampered = bytearray(g_ciphertext)
    tampered[0] ^= 1
    try:
        HwAES(pKey, HwAES.MODE_GCM, nonce).decrypt_and_verify(tampered, tag, aad)
        print("[x] GCM accepted tampered data!")
    except ValueError:
        print("[√] GCM rejected tampered data")
