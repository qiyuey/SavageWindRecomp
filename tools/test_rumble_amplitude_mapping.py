"""Offline checks for experimental amplitude mapping; never opens a controller."""
import math
import unittest
from rumble_amplitude_mapping import PROFILES, map_samples

class MappingTests(unittest.TestCase):
    def test_endpoints_monotonic_and_bounded(self):
        for drive in PROFILES.values():
            values=[map_samples([[0,a,160,0,320,a,160,0,320]],drive,10)[0][1] for a in (i/1000 for i in range(1001))]
            self.assertEqual(values[0],0);self.assertEqual(values[-1],1)
            self.assertTrue(all(a<b for a,b in zip(values,values[1:])))
            self.assertTrue(all(0<=a<=1 for a in values))

    def test_identity_and_no_source_mutation(self):
        samples=[[0,.1,160,.2,320,.3,80,.4,400],[10,0,160,0,320,0,80,0,400]]
        self.assertEqual(map_samples(samples,strength_percent=10),samples)
        out=map_samples(samples,8,10)
        self.assertEqual(samples[0][1],.1)
        self.assertEqual(out[-1],samples[-1])
        for before,after in zip(samples,out):
            for i in (0,2,4,6,8):self.assertEqual(before[i],after[i])
        self.assertAlmostEqual(out[0][1]/out[0][3],.5)
        self.assertAlmostEqual(out[0][5]/out[0][7],.75)

    def test_stereo_independence_and_silent_component(self):
        a=[0,.125,160,0,320,.2,160,.1,320]
        b=a.copy();b[5]=1
        self.assertEqual(map_samples([a],8,10)[0][1:5],map_samples([b],8,10)[0][1:5])
        self.assertAlmostEqual(map_samples([a],8,10)[0][1],8*.125/(1+7*.125))
        self.assertEqual(map_samples([a],8,10)[0][3],0)

    def test_strength_before_curve(self):
        sample=[[0,.125,160,.0625,320,.8,80,.4,400]]
        self.assertEqual(map_samples(sample,4,0)[0][1::2],[0,0,0,0])
        self.assertAlmostEqual(map_samples(sample,1,5)[0][1],.0625)
        self.assertAlmostEqual(map_samples(sample,1,20)[0][1],.25)
        self.assertAlmostEqual(map_samples(sample,4,20)[0][1],4*.25/(1+3*.25))
        self.assertAlmostEqual(map_samples(sample,1,50)[0][1],.625)
        out=map_samples(sample,1,50)[0]
        self.assertEqual(out[5],1);self.assertEqual(out[7],.5)
        self.assertEqual(map_samples(sample)[0][1],1)
        self.assertEqual(map_samples(sample)[0][3],.5)
        for strength in (-1,101,math.nan,math.inf):
            with self.assertRaises(ValueError):map_samples(sample,1,strength)

    def test_bad_parameters(self):
        for drive in (0,9,math.inf,math.nan):
            with self.assertRaises(ValueError):map_samples([],drive)
        for amp in (-1,1.1,math.inf,math.nan):
            with self.assertRaises(ValueError):map_samples([[0,amp,160,0,320,0,160,0,320]],2)

if __name__=='__main__':unittest.main()
