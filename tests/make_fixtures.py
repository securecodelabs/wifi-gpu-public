"""Independent synthetic HC22000 oracle. Python stdlib only; no captured traffic.

SHA1/MD5/SHA256/PBKDF2 use hashlib/hmac. AES/CMAC is a small independent test
implementation checked against FIPS 197 / RFC 4493 before any fixtures are made.
"""
import hashlib
import hmac
import json
from pathlib import Path


def mul(a, b):
    out = 0
    for _ in range(8):
        if b & 1:
            out ^= a
        a = ((a << 1) ^ (0x11b if a & 128 else 0)) & 255
        b >>= 1
    return out


def sbox(x):
    inv = 0 if x == 0 else 1
    for _ in range(254 if x else 0):
        inv = mul(inv, x)
    out = inv ^ 0x63
    for shift in range(1, 5):
        out ^= ((inv << shift) | (inv >> (8-shift))) & 255
    return out


SBOX = [sbox(x) for x in range(256)]


def aes(key, block):
    assert len(key) == len(block) == 16
    expanded = list(key)
    rcon = 1
    while len(expanded) < 176:
        temp = expanded[-4:]
        if len(expanded) % 16 == 0:
            temp = [SBOX[x] for x in temp[1:] + temp[:1]]
            temp[0] ^= rcon
            rcon = mul(rcon, 2)
        for b in temp:
            expanded.append(expanded[-16] ^ b)
    state = [a ^ b for a, b in zip(block, expanded)]
    for rnd in range(1, 11):
        sub = [SBOX[x] for x in state]
        state = [sub[r+4*((c+r)%4)] for c in range(4) for r in range(4)]
        if rnd != 10:
            for c in range(4):
                a, b, d, e = state[c*4:c*4+4]
                state[c*4:c*4+4] = [mul(a, 2)^mul(b, 3)^d^e, a^mul(b, 2)^mul(d, 3)^e,
                                    a^b^mul(d, 2)^mul(e, 3), mul(a, 3)^b^d^mul(e, 2)]
        state = [a ^ b for a, b in zip(state, expanded[rnd*16:(rnd+1)*16])]
    return bytes(state)


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def cmac(key, data):
    def double(x):
        n = int.from_bytes(x, 'big')
        return (((n << 1) ^ (0x87 if n >> 127 else 0)) & ((1 << 128)-1)).to_bytes(16, 'big')
    k1 = double(aes(key, bytes(16)))
    k2 = double(k1)
    if data and len(data) % 16 == 0:
        body, last = data[:-16], xor(data[-16:], k1)
    else:
        rem = len(data) % 16
        body, tail = data[:len(data)-rem], data[len(data)-rem:]
        last = xor(tail+b'\x80'+bytes(15-rem), k2)
    state = bytes(16)
    for i in range(0, len(body), 16):
        state = aes(key, xor(state, body[i:i+16]))
    return aes(key, xor(state, last))


def kdf(key, label, context, bits):
    return b''.join(hmac.digest(key, i.to_bytes(2, 'little')+label+context+bits.to_bytes(2, 'little'), 'sha256')
                    for i in range(1, (bits+255)//256+1))[:bits//8]


def record(kind=2, version=2, pair=0, ssid=b'wifi-gpu-lab', password=b'labpass42',
           correction=0, little=False, payload=22, reverse=False):
    ap, client = bytes.fromhex('020000000001'), bytes.fromhex('020000000002')
    if reverse:
        ap, client = client, ap
    anonce = bytes(range(32))
    snonce = bytes(range(32, 64))
    pmk = hashlib.pbkdf2_hmac('sha1', password, ssid, 4096, 32)
    mdid, r0, r1 = bytes.fromhex('1234'), b'test-r0-holder', bytes.fromhex('020000000005')
    r0ctx = bytes([len(ssid)])+ssid+mdid+bytes([len(r0)])+r0+client
    r1ctx = r1+client
    ft_fields = [mdid.hex(), r0.hex(), r1.hex()] if kind >= 3 else []
    flag = pair
    if kind in (1, 3):
        if kind == 1:
            flag = 3 if version == 3 else 1
            tag = hmac.digest(pmk, b'PMK Name'+ap+client, 'sha256' if version == 3 else 'sha1')[:16]
        else:
            flag = 5
            derived = kdf(pmk, b'FT-R0', r0ctx, 384)
            r0name = hashlib.sha256(b'FT-R0N'+derived[32:48]).digest()[:16]
            tag = hashlib.sha256(b'FT-R1N'+r0name+r1ctx).digest()[:16]
        return '*'.join(['WPA', f'{kind:02x}', tag.hex(), ap.hex(), client.hex(), ssid.hex(), '', '', f'{flag:02x}']+ft_fields)
    eapol = bytearray(99+payload)
    eapol[0:2] = b'\x02\x03'
    eapol[2:4] = (len(eapol)-4).to_bytes(2, 'big')
    eapol[4] = 2
    key_info = version | 0x108
    if pair in (3, 4):
        key_info |= 0x80 | 0x40 | 0x200
        flag |= 0x10
    if pair in (1, 5):
        key_info |= 0x200
    eapol[5:7] = key_info.to_bytes(2, 'big')
    eapol[9:17] = (9).to_bytes(8, 'big')
    eapol[17:49] = anonce if pair in (3, 4) else snonce
    eapol[97:99] = payload.to_bytes(2, 'big')
    eapol[99:] = bytes(i % 256 for i in range(payload))
    if kind == 4:
        pmkr0 = kdf(pmk, b'FT-R0', r0ctx, 384)[:32]
        pmkr1 = kdf(pmkr0, b'FT-R1', r1ctx, 256)
        kck = kdf(pmkr1, b'FT-PTK', snonce+anonce+ap+client, 384)[:16]
    else:
        ctx = min(ap, client)+max(ap, client)+min(anonce, snonce)+max(anonce, snonce)
        if version <= 2:
            kck = hmac.digest(pmk, b'Pairwise key expansion\0'+ctx+b'\0', 'sha1')[:16]
        else:
            kck = kdf(pmk, b'Pairwise key expansion', ctx, 384)[:16]
    tag = cmac(kck, bytes(eapol)) if version == 3 else hmac.digest(kck, bytes(eapol), 'md5' if version == 1 else 'sha1')[:16]
    external = snonce if pair in (3, 4) else anonce
    if correction:
        assert pair not in (3, 4)
        order = 'little' if little else 'big'
        changed = (int.from_bytes(external[-4:], order)+correction) % (1 << 32)
        external = external[:-4]+changed.to_bytes(4, order)
        flag |= 0x80 | (0x20 if little else 0x40)
    return '*'.join(['WPA', f'{kind:02x}', tag.hex(), ap.hex(), client.hex(), ssid.hex(), external.hex(), eapol.hex(), f'{flag:02x}']+ft_fields)


def make(destination):
    assert aes(bytes(range(16)), bytes.fromhex('00112233445566778899aabbccddeeff')).hex() == '69c4e0d86a7b0430d8cdb78070b4c55a'
    key = bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c')
    assert cmac(key, b'').hex() == 'bb1d6929e95937287fa37d129b756746'
    assert cmac(key, bytes.fromhex('6bc1bee22e409f96e93d7e117393172a')).hex() == '070a16b46b4d4144f79bdd9dd04a287c'
    destination.mkdir(parents=True, exist_ok=True)
    records = [record(version=v, pair=p, reverse=bool(p%2)) for v in (1, 2, 3) for p in range(6)]
    records += [record(kind=4, version=3, pair=p) for p in range(6)]
    records += [record(kind=1), record(kind=1, version=3), record(kind=3)]
    records += [record(ssid=b''), record(ssid=bytes(range(31))+b'\xff'), record(payload=600)]
    records += [record(version=v, correction=2, little=le) for v in (1, 2, 3) for le in (False, True)]
    records += [record(kind=4, version=3, correction=-2)]
    (destination/'all-types.hc22000').write_text('\n'.join(records)+'\n', encoding='ascii')
    (destination/'hashcat-public.hc22000').write_text('WPA*01*4d4fe7aac3a2cecab195321ceb99a7d0*fc690c158264*f4747f87f9f4*686173686361742d6573736964***\n', encoding='ascii')
    (destination/'oracle.json').write_text(json.dumps({'generator': 'make_fixtures.py (hashlib/hmac and independently checked AES-CMAC)', 'password_hex': b'labpass42'.hex(), 'record_count': len(records)}, indent=2)+'\n', encoding='utf-8')
    return records


if __name__ == '__main__':
    output = Path(__file__).parent/'fixtures'
    print(f'Created {len(make(output))} independent synthetic records in {output}')
