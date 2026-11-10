"""Offline catalog and GUI checks using synthetic data and a fake device."""
import json
from pathlib import Path
import queue
import tempfile
import threading
import tkinter as tk
import unittest
from unittest.mock import Mock, patch

import rumble_catalog_player as player


SAMPLES = [[0, .1, 80, .2, 320, .1, 80, .2, 320],
           [5, 0, 80, 0, 320, 0, 80, 0, 320]]


class CatalogTests(unittest.TestCase):
    def test_routes_keep_components_frequencies_and_original(self):
        samples = [row.copy() for row in SAMPLES]
        original = [r.copy() for r in samples]
        left = player.routed_samples(samples, '左侧')
        right = player.routed_samples(samples, '右侧')
        self.assertEqual(left[0][1:5], samples[0][1:5])
        self.assertEqual([left[0][5], left[0][7]], [0, 0])
        self.assertEqual(right[0][5:], samples[0][5:])
        self.assertEqual([right[0][1], right[0][3]], [0, 0])
        self.assertEqual(samples, original)
        player.validate_samples(left);player.validate_samples(right)

    def test_load_catalog_and_reject_duplicate_id(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'catalog.json'
            with self.assertRaises(ValueError):
                player.load_catalog(path)
            effect = dict(id=0, name='synthetic', samples=SAMPLES)
            variant = dict(effect, id='variant:0')
            path.write_text(json.dumps(dict(effects=[effect], variants=[variant])))
            _, entries = player.load_catalog(path)
            self.assertEqual([entry['id'] for entry in entries], ['base:0', 'variant:0'])
            path.write_text(json.dumps(dict(effects=[effect, effect])))
            with self.assertRaises(ValueError):
                player.load_catalog(path)


class FakeDevice:
    def __init__(self):
        self.commands = queue.Queue();self.messages = queue.Queue()
        self.quit = threading.Event();self.worker = Mock()
        self.worker.is_alive.return_value = False


class GuiTests(unittest.TestCase):
    """Exercise the player without initializing SDL or a controller."""
    def test_search_route_and_play(self):
        entry = dict(id='base:0', name='Synthetic base', duration_ms=5, samples=SAMPLES)
        root = tk.Tk();root.withdraw()
        try:
            with patch.object(player, 'Device', FakeDevice), patch.object(player, 'load_catalog', return_value=({}, [entry])):
                app = player.Player(root)
            app.record = Mock()
            self.assertEqual(app.strength_text.get(), '50% · 2.5×')
            app.search.set('Synthetic base')
            visible = app.tree.get_children()
            self.assertGreater(len(visible), 0)
            app.tree.selection_set(visible[0]);app.select()
            app.route.set('左侧');app.route_changed()
            app.strength.set(20);app.strength_changed()
            app.ready = True;app.selected_device = 7;app.devices = [dict(id=7)]
            app.start()
            queued = []
            while not app.device.commands.empty():queued.append(app.device.commands.get_nowait())
            plays = [payload for command,payload in queued if command == 'play']
            self.assertEqual(len(plays), 1)
            rows = plays[0]['entry']['samples']
            self.assertTrue(all(r[5] == r[7] == 0 for r in rows))
            self.assertEqual(rows[0][1:5], app.current['samples'][0][1:5])
            self.assertEqual(app.strength_text.get(), '20% · 1×')
            app.search.set('missing')
            self.assertIsNone(app.current)
            queued = []
            while not app.device.commands.empty():queued.append(app.device.commands.get_nowait())
            self.assertFalse(any(command == 'play' for command,payload in queued))
        finally:root.destroy()


if __name__ == '__main__':unittest.main()
