"""Summarize nmrack word-edge captures without assuming a DAC channel mapping."""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path


def inspect(path):
    ports = {}
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            key = (int(row['dsp']), int(row['port']))
            stat = ports.setdefault(key, dict(words=0, nonzero=0, accepted=0,
                max_receiver_lead_frames=0.0, nonzero_rx_addresses=Counter(),
                rx_dma_address_discontinuities=0, last_time=-1.0, next_address=None))
            time = float(row['machine_frame'])
            if time < stat['last_time']:
                raise ValueError(f'Time moved backwards on {key}')
            stat['last_time'] = time
            stat['words'] += 1
            nonzero = int(row['word']) != 0
            stat['nonzero'] += nonzero
            accepted = int(row['accepted']) != 0
            stat['accepted'] += accepted
            stat['max_receiver_lead_frames'] = max(stat['max_receiver_lead_frames'], float(row['receiver_lead']))
            address = int(row['rx_ddr'])
            if key[0] < 3 and accepted and int(row['rx_dcr']) & 0x800000:
                base = 0x6c0 + 9 * key[1]
                if not base <= address < base + 9:
                    raise ValueError(f'RX DMA outside expected nine-word bank: {key}, {address:#x}')
                expected = stat['next_address']
                if expected is not None and expected != address:
                    stat['rx_dma_address_discontinuities'] += 1
                stat['next_address'] = base + (address-base+1) % 9
                if nonzero:
                    stat['nonzero_rx_addresses'][f'{address:#x}'] += 1
            else:
                stat['next_address'] = None
    result = {}
    for (dsp, port), stat in sorted(ports.items()):
        del stat['last_time'], stat['next_address']
        result[f'dsp{dsp}_essi{port}'] = stat
    if not result:
        raise ValueError('Empty word capture; use --word-serial --diagnostics PREFIX')
    return dict(ports=result, physical_channel_mapping_validated=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    print(json.dumps(inspect(parser.parse_args().csv), indent=2))
