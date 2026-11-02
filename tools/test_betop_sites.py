"""Asset-free contract checks for the accepted-contact-effect generator hook.

Run with Python: tools/test_betop_sites.py. No game files, compiler or controller required.
"""
import re
import struct
import unittest
from types import SimpleNamespace

import game_sites as gs
import patches


def synthetic(base, offset, callee):
    # A small fabricated call sequence with the ABI/register context expected by the hook.
    words = [0x910023e1, 0xaa1603e0, 0xaa1403e2, 0,
             0xaa1403e0, 0xaa1303e1, 0x94000004,
             0xaa1403e0, 0xaa1303e1, 0x97fffffc]
    call = base + offset + 12
    words[3] = 0x94000000 | (((callee - call) // 4) & 0x3ffffff)
    image = bytearray(0x1000)
    struct.pack_into('<10I', image, offset, *words)
    nso = SimpleNamespace(image=image, text=(0, len(image)))
    gen = SimpleNamespace(nso=nso, base=base,
                          sites={'betop_effect_event': call, 'betop_effect_play': callee})
    return gen, call


class BetopSitesTest(unittest.TestCase):
    def test_relocation_and_original_call_contract(self):
        # Exercise both forward and backward BL displacements, including a relocated module base.
        for base, offset, target in [(0x7000000000, 0x100, 0x7000000800),
                                     (0x7200000000, 0x800, 0x7200000040)]:
            with self.subTest(base=hex(base)):
                gen, call = synthetic(base, offset, target)
                main = gs.Main(gen.nso, base)
                sites = {s.name: s for s in gs.SITES if s.name.startswith('betop_effect_')}
                self.assertEqual(main.locate(sites['betop_effect_event']), (call, 1))
                self.assertEqual(main.locate(sites['betop_effect_play']), (target, 1))
                emitted = patches.PATCHES['betop_effect_event'](gen, call)
                callee = f'f_{target:x}'
                self.assertEqual(len(re.findall(r'\b' + callee + r'\(c\);', emitted)), 1)
                self.assertEqual(emitted.count(f'c->x[30] = {call + 4:#x}ull;'), 1)
                callback = ('rt_betop_rumble_event((unsigned)LD32(c->x[19] + 0x90), '
                            '(const char *)(uintptr_t)LD64(c->x[21] + 16));')
                self.assertIn(callback, emitted)
                self.assertLess(emitted.index(callback), emitted.index(f'{callee}(c);'))
                # The instrumentation must not introduce guest register/flag/SP writes beyond original BL's LR.
                self.assertEqual(re.findall(r'c->(?:x\[\d+\]|sp|nzcv)\s*=', emitted), ['c->x[30] ='])

    def test_changed_register_context_is_rejected(self):
        gen, call = synthetic(0x7000000000, 0x100, 0x7000000800)
        # Change MOV x0,x22 to MOV x0,x23 without changing the branch itself.
        struct.pack_into('<I', gen.nso.image, 0x104, 0xaa1703e0)
        with self.assertRaises(AssertionError):
            patches.PATCHES['betop_effect_event'](gen, call)

    def test_wrong_callee_is_rejected(self):
        gen, call = synthetic(0x7000000000, 0x100, 0x7000000800)
        gen.sites['betop_effect_play'] += 4
        with self.assertRaises(AssertionError):
            patches.PATCHES['betop_effect_event'](gen, call)

    def test_non_link_branch_is_rejected(self):
        gen, call = synthetic(0x7000000000, 0x100, 0x7000000800)
        pos = call - gen.base
        word = struct.unpack_from('<I', gen.nso.image, pos)[0]
        struct.pack_into('<I', gen.nso.image, pos, word & ~0x80000000)
        with self.assertRaises(AssertionError):
            patches.PATCHES['betop_effect_event'](gen, call)


if __name__ == '__main__':
    unittest.main()
