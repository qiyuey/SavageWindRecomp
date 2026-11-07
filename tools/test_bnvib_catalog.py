"""Offline BNVIB and GUI checks using synthetic resources and a fake device."""
import json
from pathlib import Path
import queue
import struct
import tempfile
import threading
import tkinter as tk
import unittest
from unittest.mock import Mock, patch

from export_bnvib_catalog import Decoder, inspect_header
import rumble_catalog_player as player


def fixture(loop=False):
    body = bytes((0, 0, 255, 255, 128, 10, 64, 20))
    if loop:
        return struct.pack('<IHHIII', 12, 3, 200, 0, 2, len(body)) + body
    return struct.pack('<IHHI', 4, 3, 200, len(body)) + body


class ResourceTests(unittest.TestCase):
    def test_header_and_loop(self):
        self.assertEqual(inspect_header(fixture())['sample_count'], 2)
        self.assertIsNone(inspect_header(fixture())['loop'])
        self.assertEqual(inspect_header(fixture(True))['loop'],
                         dict(start_sample=0, end_sample=2, interval_samples=0))

    def test_reject_bad_extent_rate_and_loop(self):
        bad_rate = bytearray(fixture());struct.pack_into('<H', bad_rate, 6, 201)
        bad_loop = bytearray(fixture(True));struct.pack_into('<I', bad_loop, 12, 3)
        for blob in (b'', fixture()[:-1], fixture() + b'\0', bad_rate, bad_loop):
            with self.subTest(blob=blob):
                with self.assertRaises(ValueError):inspect_header(blob)

    def test_wrong_sdk_is_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'sdk';path.write_bytes(b'not the verified SDK')
            with self.assertRaises(ValueError):Decoder(path)

    def test_routes_keep_components_frequencies_and_original(self):
        samples = [[0, .1, 80, .2, 320, .1, 80, .2, 320],
                   [5, 0, 80, 0, 320, 0, 80, 0, 320]]
        original = [r.copy() for r in samples]
        left = player.routed_samples(samples, '左侧')
        right = player.routed_samples(samples, '右侧')
        self.assertEqual(left[0][1:5], samples[0][1:5])
        self.assertEqual([left[0][5], left[0][7]], [0, 0])
        self.assertEqual(right[0][5:], samples[0][5:])
        self.assertEqual([right[0][1], right[0][3]], [0, 0])
        self.assertEqual(samples, original)
        player.validate_samples(left);player.validate_samples(right)

    def test_optional_catalogs_and_duplicate_id(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'catalog.json'
            effect = dict(id='oot:synthetic', name='synthetic', game='oot',
                          samples=[[0, .1, 80, .2, 320, .1, 80, .2, 320],
                                   [5, 0, 80, 0, 320, 0, 80, 0, 320]])
            path.write_text(json.dumps(dict(game='oot', effects=[effect])))
            data = player.load_catalogs([('missing', path.with_name('absent')), ('test', path)])
            self.assertEqual(list(data), ['test'])
            path.write_text(json.dumps(dict(game='oot', effects=[effect, effect])))
            with self.assertRaises(ValueError):player.load_catalogs([('test', path)])


class FakeDevice:
    def __init__(self):
        self.commands = queue.Queue();self.messages = queue.Queue()
        self.quit = threading.Event();self.worker = Mock()
        self.worker.is_alive.return_value = False


class GuiTests(unittest.TestCase):
    """Exercise synthetic catalogs without initializing SDL or a controller."""
    def test_catalog_switch_search_route_and_play(self):
        samples = [[0, .1, 80, .2, 320, .1, 80, .2, 320],
                   [5, 0, 80, 0, 320, 0, 80, 0, 320]]
        oot = dict(game='oot', effects=[dict(id='oot:synthetic', name='Synthetic_One',
                   game='oot', samples=samples, sample_count=1, sample_rate_hz=200,
                   duration_ms=5, loop=None, source_file='synthetic.bnvib',
                   source_sha256='00'*32, calls=[])], call_table=[])
        botw = dict(effects=[dict(id=0, name='Synthetic base', duration_ms=5, samples=samples)])
        catalogs = {'旷野之息': (Path('catalog.json'), botw, [dict(botw['effects'][0],id='base:0')]),
                    '时之笛重制版': (Path('oot-catalog.json'), oot, oot['effects'])}
        root = tk.Tk();root.withdraw()
        try:
            with patch.object(player, 'Device', FakeDevice), patch.object(player, 'load_catalogs', return_value=catalogs):
                app = player.Player(root)
            app.record = Mock()
            app.game.set('时之笛重制版');app.change_catalog()
            self.assertEqual(len(app.entries), 1)
            for entry in app.entries:
                self.assertIn('原始资源试听', player.effect_description(entry, app.data))
                player.validate_samples(entry['samples'])
                self.assertEqual(entry['duration_ms'], entry['sample_count'] * 5)
            app.search.set('Synthetic_')
            visible = app.tree.get_children()
            self.assertGreater(len(visible), 0)
            app.tree.selection_set(visible[0]);app.select()
            app.route.set('左侧');app.strength.set(100);app.strength_changed()
            app.ready = True;app.selected_device = 7;app.devices = [dict(id=7)]
            app.start()
            queued = []
            while not app.device.commands.empty():queued.append(app.device.commands.get_nowait())
            plays = [payload for command,payload in queued if command == 'play']
            self.assertEqual(len(plays), 1)
            rows = plays[0]['entry']['samples']
            self.assertTrue(all(r[5] == r[7] == 0 for r in rows))
            self.assertEqual(rows[0][1:5], app.current['samples'][0][1:5])
            self.assertEqual(app.strength_text.get(), '100% · 1×')
            app.game.set('旷野之息');app.change_catalog()
            self.assertEqual(app.strength_text.get(), '100% · 5×')
            app.strength.set(20)
            self.assertEqual(app.playback_samples(app.entries[0])[0][1:5], samples[0][1:5])
            self.assertTrue(app.entries)
            self.assertEqual(app.entries[0]['id'], 'base:0')
            self.assertIsNone(app.current)
            queued = []
            while not app.device.commands.empty():queued.append(app.device.commands.get_nowait())
            self.assertFalse(any(command == 'play' for command,payload in queued))
        finally:root.destroy()


if __name__ == '__main__':unittest.main()
