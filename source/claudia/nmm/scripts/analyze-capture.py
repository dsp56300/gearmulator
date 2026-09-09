#!/usr/bin/env python3
"""Summarize a stopped NMM bounded capture. Never runs on an audio/worker thread."""
import argparse
import collections
import csv
import json

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('capture')
args = parser.parse_args()
with open(args.capture) as stream:
    metadata = next(stream).strip()
    events = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
workers = [e for e in events if e['kind'] == 7]
cpu = [e for e in workers if e['e']]
summary = {
    'metadata': metadata,
    'events_by_kind': dict(collections.Counter(e['kind'] for e in events)),
    'submitted_note_ons': sum(e['kind'] == 1 and e['a'] & 0xf0 == 0x90 and e['c'] > 0 for e in events),
    'mcu_note_on_entries': sum(e['kind'] == 2 and e['a'] == 0x107728 for e in events),
    'mcu_gate_entries': sum(e['kind'] == 2 and e['a'] == 0x108e76 for e in events),
    'sampled_worker_jobs': len(workers),
    'sampled_worker_wall_max_us': max((e['a'] for e in workers), default=0),
    'sampled_worker_cpu_max_us': max((e['b'] for e in cpu), default=0),
    'sampled_worker_non_cpu_max_us': max((max(0, e['a'] - e['b']) for e in cpu), default=0),
    'sampled_job_backlog_max': max((e['c'] for e in workers), default=0),
    'limitations': 'Bounded capture may end mid-message/chord. Counts alone do not prove note loss. Worker clocks are sampled; non-CPU time includes blocking/descheduling. Audio records contain last-sample snapshots, not a full waveform.'
}
print(json.dumps(summary, indent=2))
