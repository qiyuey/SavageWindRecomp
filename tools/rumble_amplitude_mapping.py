"""Uncalibrated target-device tuning, not an official HD1-to-HD2 transfer curve."""
import math

BASE_STRENGTH_GAIN = 10.0

PROFILES = {
    '线性映射（不额外增强）': 1.0,
    '弱信号增强 · 轻（实验）': 2.0,
    '弱信号增强 · 中（实验）': 4.0,
    '弱信号增强 · 强（实验）': 8.0,
}


def map_samples(samples, drive=1.0, strength_percent=100):
    """Smooth bounded expansion; one shared gain per side preserves its band ratio.

    For dominant amplitude m, output = drive*m/(1+(drive-1)*m).
    All profiles share zero and full-scale endpoints. No event normalization,
    extra carrier, minimum floor, onset kick or duration modification is applied.
    drive is an experimental curve parameter, not a measured hardware gain.
    """
    if not math.isfinite(drive) or not 1 <= drive <= 8:
        raise ValueError('Invalid amplitude curve')
    if not math.isfinite(strength_percent) or not 0 <= strength_percent <= 100:
        raise ValueError('Invalid strength percentage')
    strength=BASE_STRENGTH_GAIN*strength_percent/100
    result=[]
    for sample in samples:
        row=list(sample)
        for low, high in ((1,3),(5,7)):
            a,b=row[low],row[high]
            if not all(math.isfinite(v) and 0 <= v <= 1 for v in (a,b)):
                raise ValueError('Invalid amplitude')
            # Apply user strength before device tuning; shared saturation keeps
            # the two bands in proportion even when one reaches full scale.
            scale=strength/max(1.0,max(a,b)*strength)
            a*=scale;b*=scale
            peak=max(a,b)
            gain=drive/(1+(drive-1)*peak)
            row[low]=a*gain;row[high]=b*gain
        result.append(row)
    return result
