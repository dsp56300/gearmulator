#!/usr/bin/env python3
"""Guard against false JIT attribution at boundaries and shared addresses."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('profile_jit', Path(__file__).with_name('profile-jit.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AttributionTests(unittest.TestCase):
    def test_boundaries_aliases_and_ambiguity(self):
        maps = '1000 10 404 1 0 123456\n1000 10 305 1 0 123456\n1010 10 424 1 0 abcdef\n1018 10 503 1 0 abcdef\n'
        sample = '''Sort by top of stack, same collapsed (when >= 5):
??? [0x1000] 5
??? [0x100f] 6
??? [0x1010] 7
??? [0x1018] 8
??? [0x1028] 9
named_function [0x1000] 99
Binary Images:
??? [0x1000] 999
'''
        result = module.attribute(maps, sample)
        self.assertEqual(result['mapped_leaf_observations'], 18)
        self.assertEqual(result['unmapped_leaf_observations'], 17)
        self.assertEqual([a['dsp_pc'] for a in result['blocks'][0]['aliases']], ['0x404', '0x305'])
        self.assertEqual([u['matching_ranges'] for u in result['unmapped']], [2, 0])

    def test_invalid_map(self):
        for record in ['1000 0 404 1 0 123456', '1000 10 404 2 0 123456']:
            with self.assertRaises(ValueError):
                module.attribute(record, '')


if __name__ == '__main__':
    unittest.main()
