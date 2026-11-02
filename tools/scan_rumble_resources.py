"""Inventory controller-rumble actions in the user's local RomFS archives.

Requires oead. Resource contents and generated reports are local-only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import oead
import evfl


def scan(root):
    result={'actions':[], 'event_references':[], 'controller_events':[], 'errors':[], 'archives':0,
            'unique_ai_programs':0, 'duplicate_ai_programs':0}
    seen={}
    def action_nodes(node, source, route):
        try:
            definition=node.objects['Def']
            cls=str(definition.params['ClassName'].v)
        except KeyError:
            cls=''
        if cls in ('ControllerRumble','TimeSpecControllerRumble','RumbleController'):
            params={}
            for group in ('SInst','DInst'):
                if group in node.objects:
                    for key in ('Pattern','Count','Time','IsWait','RumbleType','RumblePower','Power'):
                        if key in node.objects[group].params:
                            value=node.objects[group].params[key].v
                            params[group+'.'+key]=value if isinstance(value,(str,int,float,bool)) else str(value)
            result['actions'].append({'source':source,'node':route,'class':cls,
                                      'name':str(definition.params['Name'].v) if 'Name' in definition.params else '', 'parameters':params})
        for name,child in node.lists.items():
            action_nodes(child,source,route+'/'+str(name))
    def visit(data,source,depth=0):
        try:
            if data[:4]==b'Yaz0':data=bytes(oead.yaz0.decompress(data))
            if data[:4]==b'SARC':
                if depth>8:raise ValueError('Archive nesting exceeds bound')
                result['archives']+=1
                for f in oead.Sarc(data).get_files():
                    if f.name and f.name.endswith(('.pack','.sbactorpack','.sbeventpack','.baiprog','.bfevfl','.bfevtm')):
                        visit(bytes(f.data),source+'::'+f.name,depth+1)
            elif source.endswith('.baiprog'):
                digest=hashlib.sha256(data).hexdigest()
                if digest in seen:
                    result['duplicate_ai_programs']+=1
                    seen[digest]['aliases'].append(source)
                else:
                    seen[digest]={'source':source,'aliases':[]}
                    result['unique_ai_programs']+=1
                    if any(x in data for x in (b'ControllerRumble',b'RumbleController')):
                        action_nodes(oead.aamp.ParameterIO.from_binary(data),source,'root')
            elif source.endswith(('.bfevfl','.bfevtm')) and (b'Rumble' in data or b'rumble' in data):
                names=sorted({m.decode(errors='replace') for m in re.findall(rb'[A-Za-z_][A-Za-z0-9_]{3,}',data) if b'Rumble' in m or b'rumble' in m})
                result['event_references'].append({'source':source,'names':names,'status':'reference only; event timing not decoded'})
                if source.endswith(('.bfevfl','.bfevtm')):
                    flow=evfl.EventFlow();flow.read(data)
                    if flow.flowchart:
                        for event in flow.flowchart.events:
                            action=event.data
                            if not hasattr(action,'actor_action'):
                                continue
                            name=str(action.actor_action.v)
                            actor=str(action.actor.v.identifier)
                            if 'ControllerRumble' in actor and 'Rumble' in name:
                                params={k:(v if isinstance(v,(int,float,bool,str)) else str(v)) for k,v in (action.params.data if action.params else {}).items()}
                                result['controller_events'].append({'source':source,'event':event.name,'actor':actor,'action':name,'parameters':params})
                    if flow.timeline:
                        for i,action in enumerate(list(flow.timeline.clips)+list(flow.timeline.oneshots)):
                            name=str(action.actor_action.v);actor=str(action.actor.v.identifier)
                            if 'ControllerRumble' in actor and 'Rumble' in name:
                                params={k:(v if isinstance(v,(int,float,bool,str)) else str(v)) for k,v in (action.params.data if action.params else {}).items()}
                                result['controller_events'].append({'source':source,'event':f'timeline:{i}','actor':actor,'action':name,'parameters':params,
                                    'timeline_start_raw':getattr(action,'start_time',getattr(action,'time',None)),
                                    'timeline_duration_raw':getattr(action,'duration',None)})
        except Exception as e:result['errors'].append({'source':source,'error':str(e)})
    paths=sorted(p for p in root.rglob('*') if p.is_file() and p.suffix in ('.pack','.sbactorpack','.sbeventpack','.baiprog','.bfevfl','.bfevtm'))
    for p in paths:visit(p.read_bytes(),p.relative_to(root).as_posix())
    result['duplicate_sources']=[v for v in seen.values() if v['aliases']]
    result['input_files']=len(paths)
    return result


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--romfs',type=Path,default=Path('extracted/romfs'))
    ap.add_argument('--output',type=Path,default=Path('build/rumble-catalog/resources.json'))
    a=ap.parse_args();r=scan(a.romfs);a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
    print({k:v for k,v in r.items() if isinstance(v,int)})
    print('actions',len(r['actions']),'event refs',len(r['event_references']),'errors',len(r['errors']))
