"""Export local BNVIB resources by executing the supplied game's SDK decoder.

No resource bytes, frequency tables or SDK implementation are embedded here.
All derived output belongs in ignored local build directories.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from nso import Nso
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import (UC_ARM64_REG_X0, UC_ARM64_REG_X1,
    UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_SP, UC_ARM64_REG_LR,
    UC_ARM64_REG_PC, UC_ARM64_REG_CPACR_EL1)

SDK_SHA256 = '3ae3cd4cf340d72bdaba9e6f66016a74c1eae13737a5f7299375ed6654b9b1c5'
PARSE, RETRIEVE = 0x6c9400, 0x6c9520
WORK, DONE = 0x2000000, 0x201ff00


def inspect_header(blob):
    if len(blob) < 12:
        raise ValueError('Truncated BNVIB header')
    header_size, version, rate = struct.unpack_from('<IHH', blob)
    if header_size not in (4, 12, 16) or version != 3 or rate != 200:
        raise ValueError('Unsupported BNVIB layout')
    start = header_size + 8
    if start > len(blob):
        raise ValueError('Truncated BNVIB metadata')
    size = struct.unpack_from('<I', blob, header_size + 4)[0]
    if not size or size % 4 or start + size != len(blob) or size // 4 > 19999:
        raise ValueError('Invalid BNVIB sample extent')
    count = size // 4
    loop = None
    if header_size >= 12:
        first, end = struct.unpack_from('<II', blob, 8)
        interval = struct.unpack_from('<I', blob, 16)[0] if header_size >= 16 else 0
        if not first <= end <= count or interval > 0x7fffffff:
            raise ValueError('Invalid BNVIB loop range')
        loop = {'start_sample': first, 'end_sample': end, 'interval_samples': interval}
    return {'sample_rate_hz': rate, 'sample_count': count, 'loop': loop}


class Decoder:
    def __init__(self, sdk):
        raw = sdk.read_bytes()
        if hashlib.sha256(raw).hexdigest() != SDK_SHA256:
            raise ValueError('SDK differs from the verified decoder build')
        nso = Nso(sdk)
        for i in range(3):
            _, offset, size = struct.unpack_from('<III', raw, 0x10 + i * 16)
            if hashlib.sha256(nso.image[offset:offset + size]).digest() != raw[0xa0+i*32:0xc0+i*32]:
                raise ValueError('SDK segment hash mismatch')
        if nso.size > WORK:
            raise ValueError('SDK overlaps decoder work memory')
        self.uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        self.uc.mem_map(0, nso.size)
        self.uc.mem_write(0, bytes(nso.image))
        self.uc.mem_map(WORK, 0x20000)
        self.uc.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)

    def call(self, address, *args):
        for reg, value in zip((UC_ARM64_REG_X0, UC_ARM64_REG_X1,
                               UC_ARM64_REG_X2, UC_ARM64_REG_X3), args):
            self.uc.reg_write(reg, value)
        self.uc.reg_write(UC_ARM64_REG_SP, WORK + 0x1f000)
        self.uc.reg_write(UC_ARM64_REG_LR, DONE)
        self.uc.emu_start(address, DONE, count=1000)
        if self.uc.reg_read(UC_ARM64_REG_PC) != DONE:
            raise ValueError('SDK decoder exceeded instruction budget')
        if self.uc.reg_read(UC_ARM64_REG_X0) != 0:
            raise ValueError('SDK decoder rejected the resource')

    def decode(self, blob):
        info = inspect_header(blob)
        if len(blob) > 0x1d000:
            raise ValueError('Resource exceeds decoder work buffer')
        self.uc.mem_write(WORK, bytes(0x400))
        self.uc.mem_write(WORK + 0x1000, blob)
        self.call(PARSE, WORK, WORK + 0x100, WORK + 0x1000, len(blob))
        header = struct.unpack('<IHH6I', self.uc.mem_read(WORK, 32))
        if (header[2], header[4]) != (info['sample_rate_hz'], info['sample_count']):
            raise ValueError('SDK and container sample metadata disagree')
        samples = []
        for i in range(info['sample_count']):
            self.call(RETRIEVE, WORK + 0x200, i, WORK + 0x100)
            value = list(struct.unpack('<4f', self.uc.mem_read(WORK + 0x200, 16)))
            samples.append([i * 1000 / info['sample_rate_hz'], *value, *value])
        # Resource files are single-device effects. Mirroring is an audition
        # policy, not a claim that the game routes the effect to both hands.
        duration = info['sample_count'] * 1000 / info['sample_rate_hz']
        samples.append([duration, 0, samples[-1][2], 0, samples[-1][4],
                        0, samples[-1][6], 0, samples[-1][8]])
        return dict(info, duration_ms=duration, samples=samples)


def export(resources, sdk, output, calls_path=None):
    from rumble_catalog_player import validate_samples
    calls = json.loads(calls_path.read_text(encoding='utf-8')) if calls_path else []
    decoder = Decoder(sdk)
    effects = []
    for path in sorted(resources.glob('*.bnvib')):
        blob = path.read_bytes()
        entry = dict(decoder.decode(blob), id='oot:' + path.stem,
                     name=path.stem, game='oot', source_file=path.name,
                     source_sha256=hashlib.sha256(blob).hexdigest(),
                     calls=[c for c in calls if c.get('BnvibFileName') == path.name],
                     routing='audition_mirror', runtime_parameters_applied=False)
        validate_samples(entry['samples'])
        effects.append(entry)
    if not effects:
        raise ValueError('No BNVIB resources found')
    names = {e['source_file'] for e in effects}
    unmatched = [c for c in calls if c.get('BnvibFileName') not in names]
    data = {'schema_version': 1, 'game': 'oot', 'title': '时之笛重制版',
            'decoder': {'sdk_sha256': SDK_SHA256, 'method': 'native_sdk_offline',
                        'parse_address': hex(PARSE), 'retrieve_address': hex(RETRIEVE)},
            'effects': effects, 'call_table': calls, 'unresolved_calls': unmatched}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    report = ['# Ocarina BNVIB audition catalog', '',
              'Native SDK-decoded resource effects; not a scene replay or native HD Rumble 2 implementation.',
              'Original sample timing and both frequency components are retained. Loop metadata is preserved;',
              'each resource is played once, followed by a player-added stop. Default routing mirrors both sides.',
              'Call-table gain/length and unresolved defaults are not applied.', '',
              f'Effects: {len(effects)}; call records: {len(calls)}; unresolved: {len(unmatched)}.', '',
              '| Resource | Duration ms | Samples | Loop metadata |', '| --- | ---: | ---: | --- |']
    report.extend(f"| {e['name']} | {e['duration_ms']:g} | {e['sample_count']} | {e['loop']} |" for e in effects)
    output.with_suffix('.md').write_text('\n'.join(report) + '\n', encoding='utf-8')
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--calls', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/rumble-catalog/oot-catalog.json'))
    args = parser.parse_args()
    data = export(args.resources, args.sdk, args.output, args.calls)
    print(f"Exported {len(data['effects'])} effects; {len(data['unresolved_calls'])} unresolved call records")
