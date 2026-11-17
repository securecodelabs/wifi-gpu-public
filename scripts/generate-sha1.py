"""Reproduce the fixed 20-byte SHA-1 bodies used by Vulkan, OpenCL and HIP."""
import argparse
from pathlib import Path
import re

BEGIN='// BEGIN GENERATED SHA1_20\n'
END='// END GENERATED SHA1_20\n'

def compression():
    lines=['Digest sha1_20(Digest base,Digest input_digest) {',
           '    uint a=base.a,b=base.b,c=base.c,d=base.d,e=base.e;']
    for i in range(16):
        value=f'input_digest.{"abcde"[i]}' if i<5 else (
            '0x80000000u' if i==5 else ('672u' if i==15 else '0u'))
        lines.append(f'    uint w{i}={value};')
    names=list('abcde')
    for i in range(80):
        a,b,c,d,e=names
        if i>=16:
            lines.append(f'    w{i&15}=rol(w{(i-3)&15}^w{(i-8)&15}^w{(i-14)&15}^w{i&15},1u);')
        if i<20: f=f'(({b}&{c})|(~{b}&{d}))'; k='0x5a827999u'
        elif i<40: f=f'({b}^{c}^{d})'; k='0x6ed9eba1u'
        elif i<60: f=f'(({b}&{c})|({b}&{d})|({c}&{d}))'; k='0x8f1bbcdcu'
        else: f=f'({b}^{c}^{d})'; k='0xca62c1d6u'
        lines.append(f'    {e}+=rol({a},5u)+{f}+{k}+w{i&15}; {b}=rol({b},30u);')
        names=[e,a,b,c,d]
    assert names==list('abcde')
    lines+=['    return Digest(a+base.a,b+base.b,c+base.c,d+base.d,e+base.e);','}']
    return '\n'.join(lines)+'\n'

def opencl_compression():
    text=compression().replace('Digest sha1_20(Digest base,Digest input_digest)',
                               'WIFI_DEVICE_INLINE void sha1_20(const uint base[5],uint digest[5])')
    for index,name in enumerate('abcde'):
        text=text.replace('input_digest.'+name,f'digest[{index}]').replace('base.'+name,f'base[{index}]')
    text=text.replace('rol(', 'rotate(')
    text=text.replace('    return Digest(a+base[0],b+base[1],c+base[2],d+base[3],e+base[4]);',
                      '    digest[0]=a+base[0]; digest[1]=b+base[1]; digest[2]=c+base[2]; digest[3]=d+base[3]; digest[4]=e+base[4];')
    text=re.sub(r'\(\((\w)&(\w)\)\|\(~\1&(\w)\)\)', r'SHA1_CH(\1,\2,\3)', text)
    text=re.sub(r'\(\((\w)&(\w)\)\|\(\1&(\w)\)\|\(\2&\3\)\)', r'SHA1_MAJ(\1,\2,\3)', text)
    return ('#if defined(WIFI_GPU_HIP) || defined(WIFI_GPU_OPENCL_GFX1200)\n'+hip_compression()+
            '#if defined(WIFI_GPU_HIP_PAIR) || defined(WIFI_GPU_OPENCL_GFX1200)\n'+hip_pair_compression()+'#endif\n'+
            '#elif defined(WIFI_GPU_AMD_OPENCL)\n'+amd_opencl_compression()+
            '#else\n'+text+'#endif\n')


def amd_schedule_step(index):
    # Squaring the XOR/rotate recurrence cancels its cross terms. Applying
    # this twice doubles, then quadruples, the dependency distances/rotation.
    if index>=64: return (12,32,56,64),4
    if index>=32: return (6,16,28,32),2
    return (3,8,14,16),1


def amd_opencl_compression():
    # Precompute the schedule so the AMD compiler can overlap its independent
    # operations with the serial round state. Keep the portable ring elsewhere.
    lines=['WIFI_DEVICE_INLINE void sha1_20(const uint base[5],uint digest[5]) {',
           '    uint a=base[0],b=base[1],c=base[2],d=base[3],e=base[4];']
    for i in range(80):
        value=f'digest[{i}]' if i<5 else (
            '0x80000000u' if i==5 else ('672u' if i==15 else '0u'))
        lines.append(f'    uint w{i}={value};')
    for i in range(16,80):
        distances,rotation=amd_schedule_step(i)
        words='^'.join(f'w{i-distance}' for distance in distances)
        lines.append(f'    w{i}=rotate({words},{rotation}u);')
    names=list('abcde')
    for i in range(80):
        a,b,c,d,e=names
        if i<20: f=f'SHA1_CH({b},{c},{d})'; k='0x5a827999u'
        elif i<40: f=f'({b}^{c}^{d})'; k='0x6ed9eba1u'
        elif i<60: f=f'SHA1_MAJ({b},{c},{d})'; k='0x8f1bbcdcu'
        else: f=f'({b}^{c}^{d})'; k='0xca62c1d6u'
        lines.append(f'    {e}=({e}+w{i}+{k})+(rotate({a},5u)+{f}); {b}=rotate({b},30u);')
        names=[e,a,b,c,d]
    assert names==list('abcde')
    lines+=['    digest[0]=a+base[0];digest[1]=b+base[1];digest[2]=c+base[2];digest[3]=d+base[3];digest[4]=e+base[4];','}']
    return '\n'.join(lines)+'\n'



# Fixed 20-byte input identities selected by the gfx1200 HIP experiment.
# Each entry is (previous word indices, rotation, XOR constant before rotation).
# Older AMD OpenCL devices retain their independent expanded schedule.
HIP_SCHEDULE = {
    16: ((0, 2), 1, 0x00000000),
    17: ((1, 3), 1, 0x00000000),
    18: ((2, 4), 1, 0x000002a0),
    19: ((3, 16), 1, 0x80000000),
    20: ((4, 17), 1, 0x00000000),
    21: ((2, 4), 2, 0x400002a0),
    22: ((3, 16), 2, 0x80000000),
    23: ((4, 17), 2, 0x00000150),
    24: ((16, 21), 1, 0x00000000),
    25: ((17, 22), 1, 0x00000000),
    26: ((18, 23), 1, 0x00000000),
    27: ((19, 24), 1, 0x00000000),
    28: ((4, 22), 2, 0x00000000),
    29: ((4, 17), 4, 0x20000104),
    30: ((0, 2, 24), 2, 0x00000000),
    31: ((1, 3, 25), 2, 0x000003f0),
    33: ((1, 17, 27), 2, 0x80000000),
    34: ((2, 18, 28), 2, 0x00000000),
    35: ((3, 19, 29), 2, 0x00000000),
    36: ((4, 20, 30), 2, 0x00000000),
    37: ((21, 31), 2, 0x80000000),
    38: ((22, 32), 2, 0x00000000),
    39: ((23, 33), 2, 0x00000000),
    40: ((24, 34), 2, 0x00000000),
    41: ((1, 29), 4, 0x00000000),
    42: ((2, 30), 4, 0x00000000),
    43: ((3, 31), 4, 0x000000a8),
    44: ((16, 28, 38), 2, 0x00000000),
    45: ((17, 29, 39), 2, 0x00000000),
    46: ((18, 30, 40), 2, 0x00000000),
    47: ((19, 31, 41), 2, 0x000002a0),
    59: ((1, 27, 47), 4, 0x80000150),
    60: ((0, 28, 48), 4, 0x00000000),
    61: ((1, 29, 49), 4, 0x00000150),
    62: ((0, 30, 50), 4, 0x00000000),
    63: ((1, 31, 51), 4, 0x00000150),
    64: ((0, 32, 52), 4, 0x00000000),
    65: ((1, 33, 53), 4, 0x00000000),
    66: ((2, 34, 54), 4, 0x00000000),
    67: ((3, 35, 55), 4, 0x00000000),
    68: ((4, 36, 56), 4, 0x00000000),
    69: ((2, 4, 45), 8, 0xe8000000),
    70: ((38, 58), 4, 0x00000000),
    71: ((4, 47), 8, 0x0000002a),
    72: ((16, 40, 60), 4, 0x00000000),
    73: ((17, 41, 61), 4, 0x00000000),
    74: ((18, 42, 62), 4, 0x00000000),
    75: ((19, 43, 63), 4, 0x00000000),
    76: ((20, 44, 64), 4, 0x00000000),
    77: ((21, 45, 65), 4, 0x00000000),
    78: ((22, 46, 66), 4, 0x00000000),
    79: ((23, 47, 67), 4, 0x000002a0),
}


def hip_compression():
    text=amd_opencl_compression()
    for index,(words,rotation,constant) in HIP_SCHEDULE.items():
        terms=[f'w{word}' for word in words]
        if constant: terms.append(f'0x{constant:08x}u')
        expression='^'.join(terms)
        if rotation: expression=f'rotate({expression},{rotation}u)'
        text,count=re.subn(rf'    w{index}=rotate\([^;]+;',
                           f'    w{index}={expression};',text,count=1)
        if count!=1: raise AssertionError(f'Missing HIP schedule word {index}')
    return text


def hip_pair_compression():
    # Interleave independent PBKDF2 blocks while sharing the HMAC pad state.
    # Expand each schedule word immediately before its round to limit live
    # registers. The same affine identities are proved by verify_hip_schedule.
    body=['    uint a=base[0],b=base[1],c=base[2],d=base[3],e=base[4];']
    for i in range(16):
        value=f'digest[{i}]' if i<5 else (
            '0x80000000u' if i==5 else ('672u' if i==15 else '0u'))
        body.append(f'    uint w{i}={value};')
    names=list('abcde')
    constants=('0x5a827999u','0x6ed9eba1u','0x8f1bbcdcu','0xca62c1d6u')
    for i in range(80):
        if i>=16:
            distances,rotation=amd_schedule_step(i)
            words,rotation,constant=HIP_SCHEDULE.get(
                i,(tuple(i-distance for distance in distances),rotation,0))
            terms=[f'w{word}' for word in words]
            if constant: terms.append(f'0x{constant:08x}u')
            body.append(f'    uint w{i}=rotate({"^".join(terms)},{rotation}u);')
        a,b,c,d,e=names
        if i<20: f=f'SHA1_CH({b},{c},{d})'
        elif 40<=i<60: f=f'SHA1_MAJ({b},{c},{d})'
        else: f=f'({b}^{c}^{d})'
        body.append(f'    {e}=({e}+w{i}+{constants[i//20]})+(rotate({a},5u)+{f}); {b}=rotate({b},30u);')
        names=[e,a,b,c,d]
    assert names==list('abcde')
    body.append('    digest[0]=a+base[0];digest[1]=b+base[1];digest[2]=c+base[2];digest[3]=d+base[3];digest[4]=e+base[4];')
    lines=['WIFI_DEVICE_INLINE void sha1_pair(const uint base[5],uint u0[5],uint u1[5]) {']
    # Stagger the two chains in groups of 80 emitted lines. On gfx1200
    # this shortens register lifetimes versus alternating every statement.
    for offset in range(0,len(body),80):
        for lane in (0,1):
            for line in body[offset:offset+80]:
                text=re.sub(r'\b([abcde]|w[0-9]+)\b',lambda m: m[1]+f'_{lane}',line)
                lines.append(text.replace('digest[',f'u{lane}['))
    return '\n'.join(lines+['}'])+'\n'


def vulkan_compression():
    # RADV benefits from the same proved fixed-padding schedule as HIP, with
    # one PBKDF2 block per invocation. Paired scalar/vector variants did not
    # improve the measured throughput enough to justify extra live state.
    text=hip_compression().replace(
        'WIFI_DEVICE_INLINE void sha1_20(const uint base[5],uint digest[5])',
        'Digest sha1_20(Digest base,Digest input_digest,uint first_round)')
    for index,name in enumerate('abcde'):
        text=text.replace(f'base[{index}]','base.'+name).replace(f'digest[{index}]','input_digest.'+name)
    start=text.index('    input_digest.a=a+base.a;')
    text=text[:start]+'    return Digest(a+base.a,b+base.b,c+base.c,d+base.d,e+base.e);\n}\n'
    text=text.replace('rotate(', 'rol(')
    # This sum depends only on the HMAC pad. Compute it once before the
    # PBKDF2 loop instead of relying on the driver to hoist it from the call.
    first='e=(e+w0+0x5a827999u)+(rol(a,5u)+SHA1_CH(b,c,d));'
    assert text.count(first)==1
    text=text.replace(first,'e=w0+first_round;')
    prepare=('uint sha1_first_round(Digest base) {\n'
             '    return base.e+0x5a827999u+rol(base.a,5u)+SHA1_CH(base.b,base.c,base.d);\n}\n')
    return ('#define SHA1_CH(b,c,d) ((d)^((b)&((c)^(d))))\n'
            '#define SHA1_MAJ(b,c,d) (((b)&(c))|((d)&((b)^(c))))\n'+prepare+text)


def verify_hip_schedule():
    # SHA-1 expansion is affine for this fixed padding. Zero plus every input
    # basis bit proves all 80 words for every possible 160-bit HMAC digest.
    def rotate(word,bits):
        bits%=32
        return ((word<<bits)|(word>>((32-bits)%32)))&0xffffffff
    for bit in range(-1,160):
        canonical=[0]*16
        if bit>=0: canonical[bit//32]=1<<(bit%32)
        canonical[5]=0x80000000; canonical[15]=672
        actual=canonical.copy()
        for index in range(16,80):
            canonical.append(rotate(canonical[index-3]^canonical[index-8]^
                                    canonical[index-14]^canonical[index-16],1))
            if index in HIP_SCHEDULE:
                words,rotation,constant=HIP_SCHEDULE[index]
            else:
                distances,rotation=amd_schedule_step(index)
                words=tuple(index-distance for distance in distances); constant=0
            value=constant
            for word in words:
                if not 0<=word<index: raise AssertionError('HIP schedule dependency')
                value^=actual[word]
            actual.append(rotate(value,rotation))
        if actual!=canonical:
            raise SystemExit(f'HIP SHA-1 schedule differs for input basis bit {bit}')
    print('PASS: HIP fixed-padding schedule matches zero and all 160 input basis bits')


def verify_amd_schedule():
    # XOR and rotation are linear over GF(2). Checking every input basis bit
    # proves the expanded recurrence matches the canonical 16-word schedule
    # for all 512-bit messages, including every fixed-padding digest input.
    def rotate(word,bits):
        return ((word<<bits)|(word>>(32-bits)))&0xffffffff
    for bit in range(512):
        canonical=[0]*16; canonical[bit//32]=1<<(bit%32)
        expanded=canonical.copy()
        for i in range(16,80):
            canonical.append(rotate(canonical[i-3]^canonical[i-8]^canonical[i-14]^canonical[i-16],1))
            distances,rotation=amd_schedule_step(i)
            word=0
            for distance in distances: word^=expanded[i-distance]
            expanded.append(rotate(word,rotation))
        if expanded!=canonical:
            raise SystemExit(f'AMD SHA-1 schedule differs for input basis bit {bit}')
    print('PASS: AMD SHA-1 schedule matches all 512 canonical input basis bits')


def main():
    p=argparse.ArgumentParser()
    options=p.add_mutually_exclusive_group(required=True)
    options.add_argument('--check',action='store_true')
    options.add_argument('--write',action='store_true')
    args=p.parse_args()
    verify_amd_schedule()
    verify_hip_schedule()
    root=Path(__file__).resolve().parent.parent
    for name,body in [('pbkdf2.comp',vulkan_compression()),('pbkdf2.cl',opencl_compression())]:
        path=root/'kernels'/name
        raw=path.read_bytes(); ending=b'\r\n' if b'\r\n' in raw else b'\n'
        text=raw.decode().replace('\r\n','\n')
        start=text.index(BEGIN)+len(BEGIN); end=text.index(END,start)
        if args.write:
            path.write_bytes((text[:start]+body+text[end:]).encode().replace(b'\n',ending))
        elif text[start:end]!=body:
            raise SystemExit(f'{name}: generated SHA-1 differs; inspect or regenerate with --write.')
        print(f'PASS: {name} generated SHA-1 matches its generator')

if __name__=='__main__': main()
