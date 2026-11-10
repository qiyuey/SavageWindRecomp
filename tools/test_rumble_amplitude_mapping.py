"""Offline strength checks; never opens a controller or changes game samples."""
import math
import unittest
from rumble_amplitude_mapping import map_samples

class StrengthTests(unittest.TestCase):
    def test_botw_original_at_twenty_percent_and_no_source_mutation(self):
        samples=[[0,.1,160,.2,320,.3,80,.4,400],[10,0,160,0,320,0,80,0,400]]
        original=[row.copy()for row in samples]
        self.assertEqual(map_samples(samples,strength_percent=20),samples)
        out=map_samples(samples,strength_percent=100)
        self.assertEqual(samples,original)
        for before,after in zip(samples,out):
            for i in (0,2,4,6,8):self.assertEqual(before[i],after[i])
        self.assertEqual(out[-1],samples[-1])

    def test_exact_single_strength_scale_without_extra_curve(self):
        sample=[[0,.01,160,.005,320,.02,80,.004,400]]
        for percent in (0,5,10,20,50,100):
            out=map_samples(sample,strength_percent=percent)[0]
            for i in (1,3,5,7):self.assertAlmostEqual(out[i],sample[0][i]*percent/20)

    def test_saturation_preserves_ratio_and_sides_are_independent(self):
        sample=[[0,.125,160,.0625,320,.8,80,.4,400]]
        out=map_samples(sample,strength_percent=100)[0]
        self.assertEqual(out[1],.625);self.assertEqual(out[3],.3125)
        self.assertEqual(out[5],1);self.assertEqual(out[7],.5)
        other=[sample[0].copy()];other[0][5]=1
        self.assertEqual(out[1:5],map_samples(other,strength_percent=100)[0][1:5])

    def test_silence_endpoints_monotonic_and_bounded(self):
        sample=[[0,.1,160,0,320,.2,80,0,400]]
        self.assertEqual(map_samples(sample,strength_percent=0)[0][1::2],[0,0,0,0])
        values=[map_samples(sample,strength_percent=i)[0][1]for i in range(101)]
        self.assertTrue(all(a<=b for a,b in zip(values,values[1:])))
        self.assertTrue(all(0<=v<=1 for v in values))
        self.assertEqual(map_samples(sample,strength_percent=100)[0][3],0)

    def test_bad_parameters(self):
        for gain in (0,-1,math.nan,math.inf):
            with self.assertRaises(ValueError):map_samples([],base_gain=gain)
        for strength in (-1,101,math.nan,math.inf):
            with self.assertRaises(ValueError):map_samples([],strength_percent=strength)
        for amplitude in (-1,1.1,math.inf,math.nan):
            with self.assertRaises(ValueError):map_samples([[0,amplitude,160,0,320,0,160,0,320]])

if __name__=='__main__':unittest.main()
