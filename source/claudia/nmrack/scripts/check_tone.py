"""Check a diagnostic nmrack mixer WAV. Does not certify physical output routing."""
import argparse
import json
import math
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError('Not a WAV')
    chunks = {}
    offset = 12
    while offset + 8 <= len(data):
        name, size = struct.unpack_from('<4sI', data, offset)
        if offset+8+size > len(data):
            raise ValueError('Truncated WAV chunk')
        chunks[name] = data[offset+8:offset+8+size]
        offset += 8+size+(size & 1)
    fmt, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', chunks[b'fmt '])
    if (fmt, channels, rate, bits) != (3, 4, 96000, 32):
        raise ValueError('Expected four-channel 96-kHz float diagnostic WAV')
    raw = chunks[b'data']
    values = struct.unpack(f'<{len(raw)//4}f', raw)
    if len(values) != 96000*4 or not all(math.isfinite(v) for v in values):
        raise ValueError('Expected exactly 96000 finite frames')
    rms = []
    for channel in range(4):
        samples = values[channel::4]
        mean = sum(samples)/len(samples)
        rms.append(math.sqrt(sum((v-mean)**2 for v in samples)/len(samples)))
    channel = max(range(4), key=rms.__getitem__)
    if not .01 < rms[channel] < .2:
        raise ValueError('Missing/invalid AC tone')
    samples = values[channel::16]  # decimate 96 kHz to 24 kHz for frequency check
    mean = sum(samples)/len(samples)
    weighted = [(v-mean)*(.5-.5*math.cos(2*math.pi*i/(len(samples)-1))) for i, v in enumerate(samples)]
    def power(hz):
        angle = 2*math.pi*hz/24000
        re = sum(v*math.cos(angle*i) for i, v in enumerate(weighted))
        im = sum(v*math.sin(angle*i) for i, v in enumerate(weighted))
        return re*re+im*im
    peak = max(range(300, 361), key=power)
    if not 328 <= peak <= 332:
        raise ValueError(f'Unexpected oscillator frequency: {peak}')
    return dict(frames=96000, ac_rms=rms, peak_hz=peak, active_channel_zero_based=channel,
                physical_channel_mapping_validated=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wav', type=Path)
    print(json.dumps(inspect(parser.parse_args().wav), indent=2))
