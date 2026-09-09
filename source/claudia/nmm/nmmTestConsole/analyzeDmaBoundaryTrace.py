#!/usr/bin/env python3
"""Summarize offline boundary observations; never claim instruction-exact DMA events."""
import argparse
import csv
import json
from pathlib import Path


def summarize(path):
    with path.open() as stream:
        header = stream.readline().strip()
        rows = list(csv.DictReader(stream))
    arms = 0
    completions = 0
    erased = []
    previous = None
    for fields in rows:
        row = {key: int(value) for key, value in fields.items()}
        if previous is not None:
            if previous["pending_clocks"] == 0 and row["pending_clocks"] > 0:
                arms += 1
            if previous["pending_clocks"] > 0 and row["pending_clocks"] == 0:
                completions += 1
            if previous["phase"] == 1 and row["phase"] == 2:
                cleared = [key for key in ("y6c0", "y6c1", "y6e0", "y6e1")
                           if previous[key] != 0 and row[key] == 0]
                if cleared and previous["pending_clocks"] > 0:
                    erased.append(dict(cycles=row["cycles"], frame=row["frame"],
                                       pc=hex(row["pc"]), next_pc=hex(row["next_pc"]), cleared=cleared,
                                       pending_before=previous["pending_clocks"]))
        previous = row
    return dict(file=str(path), header=header, observations=len(rows),
                arms_observed=arms, completions_observed=completions,
                polls_clearing_nonzero_mix_words=len(erased), examples=erased[:8])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", type=Path, nargs="+")
    args = parser.parse_args()
    print(json.dumps([summarize(path) for path in args.traces], indent=2))


if __name__ == "__main__":
    main()
