"""Single strength control for local rumble playback; no extra device-tuning curve."""
import math

BASE_STRENGTH_GAIN = 5.0


def catalog_strength_gain(data):
    """User-selected audition scales, not hardware capability detection."""
    return 1.0 if data.get('game') == 'oot' else BASE_STRENGTH_GAIN


def map_samples(samples, strength_percent=100, base_gain=BASE_STRENGTH_GAIN):
    """Scale by the catalog's full-strength gain (BOTW 5x; Ocarina 1x).

    Timing and frequencies are unchanged. Saturation uses one shared gain per
    side, preserving the low/high amplitude ratio instead of clipping each band
    independently. There is no nonlinear weak-signal boost after this scale.
    """
    if not math.isfinite(strength_percent) or not 0 <= strength_percent <= 100:
        raise ValueError('Invalid strength percentage')
    if not math.isfinite(base_gain) or base_gain <= 0:
        raise ValueError('Invalid base strength gain')
    strength=base_gain*strength_percent/100
    result=[]
    for sample in samples:
        row=list(sample)
        for low, high in ((1,3),(5,7)):
            a,b=row[low],row[high]
            if not all(math.isfinite(v) and 0 <= v <= 1 for v in (a,b)):
                raise ValueError('Invalid amplitude')
            scale=strength/max(1.0,max(a,b)*strength)
            row[low]=a*scale;row[high]=b*scale
        result.append(row)
    return result
