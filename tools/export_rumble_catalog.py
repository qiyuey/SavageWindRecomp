"""Export local 1.9.0 controller patterns by executing the player's own code.

No game pattern strings or decompiled implementation are embedded here. Output
is derived game data and must stay in ignored local directories.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from nso import Nso
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_LR, UC_ARM64_REG_SP, UC_ARM64_REG_CPACR_EL1

BUILD = 'cd57b23fa4bbad65'
BASE, WORK = 0x7000000000, 0x7800000000
UPDATE, EMIT, STOP = 0x1329dbc, 0x132a250, 0x132a368
TABLE = 0x1da4ac8


def simulate(nso, pattern, repeats=1, seconds=None):
    """Run the actual native addon update, intercepting only the final HID sink."""
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, (len(nso.image)+4095)&~4095)
    uc.mem_write(BASE, bytes(nso.image))
    uc.mem_map(WORK, 0x40000)
    obj, vtable, string, stack, done = WORK, WORK+0x1000, WORK+0x2000, WORK+0x30000, WORK+0x3f000
    def put(off, fmt, *values):
        uc.mem_write(obj+off, struct.pack('<'+fmt, *values))
    put(0, 'Q', vtable)
    put(0x28, 'Q', string)
    put(0x34, 'I', 0 if seconds is not None else repeats)
    # Constructor's default scale. Other runtime callers may change it.
    put(0x3c, 'f', .5)
    uc.mem_write(vtable+0x28, struct.pack('<QQ', BASE+0x1329ee0, BASE+0x1329ef4))
    uc.mem_write(string, pattern.encode('ascii')+b'\0')
    uc.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
    emitted = []
    def hook(machine, address, size, _):
        if address == BASE+EMIT:
            pointer = machine.reg_read(UC_ARM64_REG_X1)
            emitted.append(struct.unpack('<4f', machine.mem_read(pointer,16)))
            machine.reg_write(UC_ARM64_REG_PC, machine.reg_read(UC_ARM64_REG_LR))
        elif address == done:
            machine.emu_stop()
    from unicorn.arm64_const import UC_ARM64_REG_PC
    uc.hook_add(UC_HOOK_CODE, hook)
    samples = []
    stop_frame = round(seconds*120) if seconds is not None else None
    for frame in range((stop_frame if stop_frame is not None else len(pattern)*repeats)+256):
        if stop_frame is not None and frame == stop_frame:
            # The observed stop API clears the pointer and asks the next update
            # to invoke the original release handler; let native decay finish.
            put(0x28, 'Q', 0)
            put(0x30, 'I', 1)
        emitted.clear()
        uc.reg_write(UC_ARM64_REG_X0,obj)
        uc.reg_write(UC_ARM64_REG_SP,stack)
        uc.reg_write(UC_ARM64_REG_LR,done)
        uc.emu_start(BASE+UPDATE,done,count=20000)
        if uc.reg_read(UC_ARM64_REG_PC) != done:
            raise RuntimeError('Native update did not return within instruction budget')
        if not emitted:
            raise RuntimeError('Native update produced no HID value')
        low,lf,high,hf = emitted[-1]
        samples.append([frame*1000/120, low,lf,high,hf,low,lf,high,hf])
        pointer = struct.unpack('<Q',uc.mem_read(obj+0x28,8))[0]
        if pointer==0 and low==0 and high==0:
            return samples
    raise RuntimeError('Native pattern failed to reach silence')


def export(main, output, evidence=None, resources=None):
    n = Nso(main)
    if not n.build_id.startswith(BUILD):
        raise ValueError('Only verified game build '+BUILD+' is supported')
    if n.image[UPDATE:UPDATE+4] != bytes.fromhex('ffc300d1'):
        raise ValueError('Native update signature changed')
    effects=[]
    for index in range(6):
        pointer=struct.unpack_from('<Q',n.image,TABLE+index*16+8)[0]
        pattern=n.cstr(pointer)
        if not pattern or set(pattern)-set('*-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'):
            raise ValueError('Unexpected pattern alphabet')
        samples=simulate(n,pattern)
        label=['Small','Middle','Large','TimeSpec Small','TimeSpec Middle','TimeSpec Large'][index]
        effects.append({'id':index,'name':label,
                        'pattern_address':hex(pointer),'pattern':pattern,
                        'nominal_pattern_ms':len(pattern)*1000/120,
                        'duration_ms':samples[-1][0], 'default_scale':.5,
                        'peak':max(s[1] for s in samples),'samples':samples})
    callers=[]
    if evidence and evidence.exists():
        callers=[{k:v for k,v in r.items() if k!='pseudo'} for r in json.loads(evidence.read_text(encoding='utf-8'))]
    resource_data=json.loads(resources.read_text(encoding='utf-8-sig')) if resources and resources.exists() else {}
    variants={}
    for item in resource_data.get('controller_events',[]):
        action=item['action'];params=item['parameters'];timed='TimeSpec' in action
        index=next((i for i,size in enumerate(('Small','Middle','Large')) if action.endswith(size)),None)
        if index is None:continue
        index+=3 if timed else 0
        repeat=max(1,int(params.get('Count',1)));seconds=float(params['Seconds']) if timed else None
        key=f'{index}:seconds={seconds}' if timed else f'{index}:count={repeat}'
        if key not in variants:
            samples=simulate(n,effects[index]['pattern'],repeat,seconds)
            variants[key]={'id':key,'name':action,'pattern_id':index,'repeat':repeat,'seconds':seconds,
                           'duration_ms':samples[-1][0],'samples':samples,'sources':[]}
        variants[key]['sources'].append(item)
    result={'schema':1,'build_id':n.build_id,'main_sha256':hashlib.sha256(Path(main).read_bytes()).hexdigest(),
            'effects':effects,'callers':callers,
            'variants':list(variants.values()),'resource_inventory':resource_data,
            'coverage':'All six verified built-in controller presets; not all contextual instances or resource-driven overrides.',
            'timing':'120 Hz nominal from native length/120 helper; OS scheduling can differ.',
            'method':'Native ARM64 addon execution in Unicorn, original game bytes; default constructor scale, one cycle, fresh state; HID routing mirrored to both sides.',
            'excluded':['Camera-only rumble','Unknown resource overrides','Concurrent effects, priority and contextual power changes']}
    output.mkdir(parents=True,exist_ok=True)
    (output/'catalog.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    lines=['# Local game controller-rumble catalog','',f'Build: `{n.build_id}`','',result['coverage'],'',result['method'],'',result['timing'],'',
           '| ID | Pattern ticks | Nominal pattern ms | Final silence ms | Peak low amplitude |','|---|---:|---:|---:|---:|']
    for r in effects:lines.append(f"| {r['id']} | {len(r['pattern'])} | {r['nominal_pattern_ms']:.2f} | {r['duration_ms']:.2f} | {r['peak']:.6f} |")
    lines+=['','## Native call-site evidence','','Strings are context hints, not verified gameplay labels.']
    for c in callers:lines+=['',f"- `{c['function']}`: {'; '.join(c['calls'])}",f"  Context strings: {', '.join(c['strings']) or '(none)'}"]
    lines+=['','## Resource call variants','',f"{len(resource_data.get('controller_events',[]))} call records, {len(variants)} distinct parameter combinations.",
            '', '| Variant | Action | Duration including release (ms) | Source references |','|---|---|---:|---:|']
    for key,v in variants.items():lines.append(f"| {key} | {v['name']} | {v['duration_ms']:.2f} | {len(v['sources'])} |")
    lines+=['','## AI controller behavior definitions','']
    for item in resource_data.get('actions',[]):lines.append(f"- `{item['source']}`: {item['class']} / {item['name']} / {item['parameters']}")
    lines+=['','## Event resource references','']
    for v in variants.values():
        for item in v['sources']:lines.append(f"- `{item['source']}` / `{item['event']}` -> `{v['id']}`; {item['parameters']}")
    (output/'catalog.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--main',type=Path,default=Path('extracted/exefs/main'))
    p.add_argument('--output',type=Path,default=Path('build/rumble-catalog'))
    p.add_argument('--evidence',type=Path,default=Path('build/rumble-catalog/all-preset-callers.json'))
    p.add_argument('--resources',type=Path,default=Path('build/rumble-catalog/resources.json'))
    a=p.parse_args();r=export(a.main,a.output,a.evidence,a.resources)
    print('Exported',len(r['effects']),'native presets to',a.output)
