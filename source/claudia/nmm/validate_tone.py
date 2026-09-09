#!/usr/bin/env python3
"""Check the headless oscillator capture without third-party dependencies."""
import math
import struct
import sys

raw = open(sys.argv[1], 'rb').read()
assert raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', 'Not a RIFF WAV'
chunks = {}
offset = 12
while offset + 8 <= len(raw):
    name, size = struct.unpack_from('<4sI', raw, offset)
    chunks[name] = raw[offset + 8:offset + 8 + size]
    offset += 8 + size + (size & 1)
fmt, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', chunks[b'fmt '])
assert (fmt, channels, rate, bits) == (3, 2, 96000, 32), 'Unexpected audio format'
samples = struct.unpack('<%df' % (len(chunks[b'data']) // 4), chunks[b'data'])
assert len(samples) == 960000, 'Expected exactly five seconds of stereo audio'
assert all(math.isfinite(v) for v in samples), 'Non-finite samples'
# Check the active channel, after the OS master-volume ramp.
audio = [samples[192000 + 2*i + 1] for i in range(16384)]
mean = sum(audio) / len(audio)
audio = [v - mean for v in audio]
rms = math.sqrt(sum(v*v for v in audio) / len(audio))
assert rms > 1e-5, 'Silence or DC'

def correlation(lag):
    a, b = audio[lag::8], audio[:-lag:8]
    return sum(x*y for x,y in zip(a,b)) / math.sqrt(sum(x*x for x in a)*sum(y*y for y in b))

best = max(range(48, 4800, 4), key=correlation)
best = max(range(max(1, best-4), best+5), key=correlation)
corr = correlation(best)
assert corr > 0.99, 'Signal is not strongly periodic'
print('PASS: 480000 frames, AC RMS %.8f, periodic correlation %.8f at lag %d' % (rms, corr, best))
