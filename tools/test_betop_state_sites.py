"""Reject charge/lookalike swaps and changed sampler helper relationships."""
import struct
import unittest
from types import SimpleNamespace
import game_sites as gs


def fixture():
    base = gs.MAIN_BASE
    sites = {name: base + off for name, off in {
        'betop_state_bow': 0x100, 'betop_state_upper': 0x300,
        'betop_state_lower': 0x400, 'betop_state_list': 0x500,
        'betop_state_name': 0x600, 'betop_state_guard': 0x700}.items()}
    image = bytearray(0x1000)
    bow = sites['betop_state_bow']
    for index, (offset, text) in enumerate([(16, b'BowCharge'), (52, b'SquatBowCharge'),
                                           (72, b'WallBowChargeL'), (100, b'WallBowChargeR')]):
        target = base + 0x800 + index * 32
        pc = bow + offset - 4
        delta = ((target & ~0xfff) - (pc & ~0xfff)) >> 12
        adrp = 0x90000001 | ((delta & 3) << 29) | (((delta >> 2) & 0x7ffff) << 5)
        add = 0x91000021 | ((target & 0xfff) << 10)
        struct.pack_into('<II', image, pc - base, adrp, add)
        pos = target - base
        image[pos:pos + len(text) + 1] = text + b'\0'
    for address, key in [(bow + 24, 'upper'), (bow + 60, 'upper'), (bow + 80, 'lower'),
                         (bow + 108, 'lower'), (sites['betop_state_upper'] + 36, 'list'),
                         (sites['betop_state_lower'] + 28, 'list'), (sites['betop_state_list'] + 76, 'name')]:
        target = sites['betop_state_' + key]
        word = 0x94000000 | (((target - address) // 4) & 0x3ffffff)
        struct.pack_into('<I', image, address - base, word)
    return gs.Main(SimpleNamespace(image=image, text=(0, len(image))), base), sites


class StateSites(unittest.TestCase):
    def test_verified_relationships(self):
        main, sites = fixture()
        gs.validate_betop_state(main, sites)

    def test_shoot_or_reload_is_not_charge(self):
        for text in [b'BowReload', b'BowShoot\0']:
            main, sites = fixture()
            main.nso.image[0x800:0x809] = text
            with self.assertRaises(gs.SiteError):
                gs.validate_betop_state(main, sites)

    def test_wrong_slot_getter_rejected(self):
        main, sites = fixture()
        sites['betop_state_upper'] += 4
        with self.assertRaises(gs.SiteError):
            gs.validate_betop_state(main, sites)

    def test_stale_known_fallback_rejected(self):
        main, sites = fixture()
        before = gs.SITES
        try:
            gs.SITES = [gs.Site('betop_state_bow', 'code', 'ffffffff ffffffff', 0)]
            with self.assertRaisesRegex(gs.SiteError, 'refusing table fallback'):
                gs.resolve(main, sites)
        finally:
            gs.SITES = before


if __name__ == '__main__':
    unittest.main()
