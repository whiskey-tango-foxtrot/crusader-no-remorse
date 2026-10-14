#!/usr/bin/env python3
"""Export Crusader SOUND.FLX to WAV.

Entry 0 is an index: repeated { u32 data, cstring name }.
Other entries: 'ASFX' + 28 bytes header, then raw 8-bit signed PCM at 11025 Hz
(ScummVM audio/sound_flex.cpp: RawAudioSample(buf+32, size-32, 11025, signed=true, stereo=false)).
"""
import struct, sys, os, wave, argparse
sys.path.insert(0, os.path.dirname(__file__))
from flx import Flex

def read_index(data):
    names = {}
    p = 0; i = 1
    while p + 4 < len(data):
        val, = struct.unpack_from('<I', data, p); p += 4
        e = data.find(b'\0', p)
        if e < 0: break
        names[i] = (data[p:e].decode('latin1'), val); p = e + 1; i += 1
    return names

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('flx'); ap.add_argument('outdir')
    a = ap.parse_args()
    fx = Flex(a.flx)
    os.makedirs(a.outdir, exist_ok=True)
    names = read_index(fx.get(0))
    with open(os.path.join(a.outdir, 'index.txt'), 'w') as fh:
        for i, (n, v) in sorted(names.items()):
            fh.write(f'{i}\t{n}\t0x{v:08x}\n')
    n = 0
    for i in range(1, len(fx)):
        d = fx.get(i)
        if not d: continue
        if d[:4] != b'ASFX':
            print(f'entry {i}: unknown header {d[:4]!r}'); continue
        pcm = bytes((b + 128) & 0xFF for b in d[32:])   # signed -> unsigned for WAV 8-bit
        name = names.get(i, ('', 0))[0]
        fn = f'{i:03d}_{name}.wav' if name else f'{i:03d}.wav'
        with wave.open(os.path.join(a.outdir, fn), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(1); w.setframerate(11025); w.writeframes(pcm)
        n += 1
    print(f'{n} sounds written, {len(names)} names')

if __name__ == '__main__':
    main()
