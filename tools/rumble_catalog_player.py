"""Local-only native controller-rumble catalog player (no game data embedded)."""
import bisect
import ctypes as C
import json
import os
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk
from rumble_amplitude_mapping import BASE_STRENGTH_GAIN, PROFILES, map_samples

ROOT=Path(__file__).resolve().parents[1]
CATALOG=ROOT/'build/rumble-catalog/catalog.json'


def validate_samples(samples):
    previous=-1
    if not samples or len(samples)>20000:raise ValueError('Invalid sample count')
    for row in samples:
        if len(row)!=9 or not all(isinstance(v,(float,int)) and float('-inf')<v<float('inf') for v in row):
            raise ValueError('Invalid sample')
        if not previous<=row[0]<=120000:raise ValueError('Invalid sample time')
        previous=row[0]
        if any(not 0<=row[i]<=1 for i in (1,3,5,7)):raise ValueError('Amplitude outside range')
        if any(not 0<=row[i]<=20000 for i in (2,4,6,8)):raise ValueError('Frequency outside range')
    if any(samples[-1][i] for i in (1,3,5,7)):raise ValueError('Missing final silence')


class Device:
    def __init__(self):
        self.commands=queue.Queue();self.messages=queue.Queue();self.quit=threading.Event()
        self.worker=threading.Thread(target=self.run,daemon=True);self.worker.start()

    def run(self):
        pad=None;s=None;cookie=None;finished=None;selected=None
        try:
            cookie=os.add_dll_directory(str(ROOT/'build'))
            usb=C.CDLL(str(ROOT/'build/libusb-1.0.dll'))
            s=C.CDLL(str(ROOT/'build/SDL3.dll'))
            def bind(name,r,args):
                f=getattr(s,name);f.restype=r;f.argtypes=args;return f
            init=bind('SDL_Init',C.c_bool,[C.c_uint]);hint=bind('SDL_SetHint',C.c_bool,[C.c_char_p,C.c_char_p])
            error=bind('SDL_GetError',C.c_char_p,[]);update=bind('SDL_UpdateGamepads',None,[])
            get=bind('SDL_GetGamepads',C.POINTER(C.c_uint),[C.POINTER(C.c_int)])
            vendor=bind('SDL_GetGamepadVendorForID',C.c_ushort,[C.c_uint]);product=bind('SDL_GetGamepadProductForID',C.c_ushort,[C.c_uint])
            name=bind('SDL_GetGamepadNameForID',C.c_char_p,[C.c_uint])
            op=bind('SDL_OpenGamepad',C.c_void_p,[C.c_uint]);close=bind('SDL_CloseGamepad',None,[C.c_void_p])
            attached=bind('SDL_GamepadConnected',C.c_bool,[C.c_void_p]);free=bind('SDL_free',None,[C.c_void_p])
            send=bind('SDL_RumbleGamepadFrequenciesEx',C.c_bool,[C.c_void_p]+[C.c_float]*8+[C.c_uint])
            stop=bind('SDL_RumbleGamepad',C.c_bool,[C.c_void_p,C.c_ushort,C.c_ushort,C.c_uint])
            shutdown=bind('SDL_Quit',None,[])
            hint(b'SDL_JOYSTICK_HIDAPI_SWITCH2_EXPERIMENTAL_HD',b'1');hint(b'SDL_JOYSTICK_HIDAPI_SWITCH2_HD_GAIN',b'1')
            if not init(0x2000):raise RuntimeError(error().decode(errors='replace'))
            playback=None;start=0.;index=0;late=0.;last=0.;entry_id=None
            def finish(reason):
                nonlocal playback
                if pad:stop(pad,0,0,0)
                if playback is not None:
                    self.messages.put(('finished',{'id':entry_id,'device_id':selected,'reason':reason,'max_api_late_ms':late,'elapsed_ms':(time.monotonic()-start)*1000}))
                playback=None
            finished=finish
            def choose(device_id):
                nonlocal pad,selected
                finish('device_changed')
                if pad:close(pad)
                pad=None;selected=None
                if device_id is not None:
                    pad=op(device_id)
                    if pad:selected=device_id
                self.messages.put(('selection',selected))
                if pad:self.messages.put(('ready','设备已连接；点击播放才震动'))
                else:self.messages.put(('error','未选择设备' if device_id is None else '无法打开设备：'+error().decode(errors='replace')))

            def refresh(initial=False):
                finish('refreshed');update()
                n=C.c_int();ids=get(C.byref(n));devices=[]
                try:
                    for i in range(n.value):
                        uid=int(ids[i]);v=vendor(uid);p=product(uid)
                        title=(name(uid) or b'Unknown').decode(errors='replace')
                        devices.append({'id':uid,'name':title,'vendor':v,'product':p,
                                        'label':f'{title}  [{v:04X}:{p:04X} · ID {uid}]'})
                finally:free(ids)
                self.messages.put(('devices',devices))
                if initial and devices:
                    preferred=next((d for d in devices if (d['vendor'],d['product'])==(0x057e,0x2069)),devices[0])
                    choose(preferred['id'])
                elif selected is not None and any(d['id']==selected for d in devices) and pad and attached(pad):
                    self.messages.put(('selection',selected))
                    self.messages.put(('ready','设备列表已刷新；当前设备保持选中，未播放'))
                else:choose(None)
                if not devices:self.messages.put(('error','未发现手柄；连接后点击刷新，无需重启工具'))

            refresh(initial=True)
            while not self.quit.is_set():
                update()
                if pad and not attached(pad):
                    choose(None);refresh()
                while not self.commands.empty():
                    command,payload=self.commands.get_nowait()
                    finish('stopped')
                    if command=='refresh':refresh()
                    elif command=='select':choose(payload)
                    elif command=='play':
                        # Bind queued playback to the device selected when Play was clicked.
                        if not pad or payload['device_id']!=selected:continue
                        entry=payload['entry'];validate_samples(entry['samples'])
                        playback=entry['samples'];entry_id=entry['id'];index=0;late=0.;start=time.monotonic();last=start
                if playback is not None:
                    elapsed=(time.monotonic()-start)*1000
                    # Keep absolute sample times; do not slow the effect to GUI rate.
                    while index<len(playback) and playback[index][0]<=elapsed:
                        row=playback[index];late=max(late,elapsed-row[0])
                        if not send(pad,*row[1:],100):raise RuntimeError(error().decode(errors='replace'))
                        update();index+=1;last=time.monotonic()
                    if index==len(playback):finish('completed')
                    elif time.monotonic()-last>.04 and index:
                        if not send(pad,*playback[index-1][1:],100):raise RuntimeError(error().decode(errors='replace'))
                        last=time.monotonic()
                self.quit.wait(.001)
        except Exception as exc:self.messages.put(('error',str(exc)))
        finally:
            if s:
                try:
                    if pad:
                        if finished:finished('closed')
                        stop(pad,0,0,0)
                        for _ in range(20):update();time.sleep(.005)
                        close(pad)
                    shutdown()
                except Exception:pass
            if cookie:cookie.close()


SIZE_NAMES=('小震动','中震动','大震动','持续小震动','持续中震动','持续大震动')
SCENE_EVIDENCE=(
 (0,'0xc6e0f8','DamageS','轻受击'),(0,'0xc83da4','DamageM','中等受击'),
 (1,'0xc6c4cc','CutReverse','反手斩'),(1,'0xc8279c','DamageL','重受击'),
 (2,'0xcec378','GuardJustHit','完美防御命中'))


def pattern_id(entry):
    return entry.get('pattern_id',int(str(entry['id']).split(':')[-1]) if str(entry['id']).startswith('base:') else 0)


def effect_name(entry):
    name=SIZE_NAMES[pattern_id(entry)]
    if str(entry['id']).startswith('base:'):
        return '基础 · '+name+('（单周期）' if pattern_id(entry)>=3 else '')
    if entry.get('seconds') is not None:
        return f"剧情 · {name} · {entry['seconds']:g}秒"
    return f"剧情 · {name} · {entry.get('repeat',1)}次"


def confirmed_scenes(entry,data):
    if not str(entry['id']).startswith('base:'):
        return '剧情资源调用已确认；具体情节未核实'
    found=[]
    for index,address,context,label in SCENE_EVIDENCE:
        if pattern_id(entry)==index and any(c['function']==address and context in c.get('strings',[]) for c in data.get('callers',[])):
            found.append(label)
    return ' / '.join(found) if found else '定时持续用节奏；具体场景未核实'


def effect_description(entry,data):
    pid=pattern_id(entry);rows=entry['samples'];peak=max(max(r[1],r[3],r[5],r[7]) for r in rows)
    lines=[effect_name(entry),'', '这项在测什么',
      '目录保存游戏原始强弱包络，双侧同步回放；实验映射的输出另见上方峰值与曲线。',
      f"原始节奏编号：{pid}；回测总时长（含收尾）：{entry['duration_ms']/1000:.3f}秒",
      f'峰值振幅：{peak:.5f}（输入满幅为1；不是实际震动力百分比）',
      '本目录波形：低频160 Hz；高频分量振幅为0。', '', '场景证据',confirmed_scenes(entry,data)]
    if str(entry['id']).startswith('base:'):
        lines+=['同一个基础节奏会被多个场景复用，以上不是唯一用途。']
        if pid>=3:lines+=['本项仅播放一个周期；指定持续秒数请选下方的剧情组合。']
    else:
        lines+=['以下只证明该资源调用了此效果；未把未核实的Demo编号猜译成剧情名称。']
        for item in entry.get('sources',[]):
            params=item.get('parameters',{});labels={'Count':'原始次数','Seconds':'持续秒数','IsWaitFinish':'等待效果结束','IsWait':'等待'}
            text='，'.join(f"{labels.get(k,k)}={('是' if v else '否') if isinstance(v,bool) else v}" for k,v in params.items())
            lines += ['',f"来源：{item['source']}",f"事件：{item['event']}；动作：{item['action']}",text]
            if 'timeline_start_raw' in item:
                lines.append(f"时间轴原始位置：{item['timeline_start_raw']}；原始跨度：{item.get('timeline_duration_raw')}（不推定单位）")
    lines += ['', '验证边界','这是独立效果，不重现距离衰减、场景并发、优先级覆盖。',
              'Switch 2输出使用待标定的专利频率映射；实际震感需要对照测试。',f"检索ID：{entry['id']}；原始名称：{entry['name']}"]
    return '\n'.join(lines)


class Player:
    def __init__(self,root):
        self.root=root;self.current=None;self.ready=False;self.devices=[];self.selected_device=None
        self.data=json.loads(CATALOG.read_text(encoding='utf-8'))
        self.entries=[dict(e,id='base:'+str(e['id'])) for e in self.data['effects']]+self.data.get('variants',[])
        for e in self.entries:validate_samples(e['samples'])
        root.title('旷野之息 · 原生震动目录与回测')
        dpi=root.winfo_fpixels('1i');scale=dpi/96
        # Fit the usable desktop instead of multiplying past the screen at 200%.
        from ctypes import wintypes
        rect=wintypes.RECT()
        if C.windll.user32.SystemParametersInfoW(48,0,C.byref(rect),0):
            screen_w,screen_h=rect.right-rect.left,rect.bottom-rect.top
        else:screen_w,screen_h=root.winfo_screenwidth(),root.winfo_screenheight()
        width=min(int(1200*scale),screen_w-40);height=min(int(820*scale),screen_h-70)
        root.geometry(f'{width}x{height}')
        root.minsize(min(int(780*scale),width),min(int(520*scale),height))
        style=ttk.Style();style.theme_use('clam');style.configure('.',font=('Microsoft YaHei UI',10))
        style.configure('Treeview',rowheight=max(24,int(28*scale)))
        frame=ttk.Frame(root,padding=int(12*scale));frame.pack(fill='both',expand=True)
        frame.columnconfigure(0,weight=1);frame.rowconfigure(7,weight=1)
        ttk.Label(frame,text='旷野之息 · 原生震动回测',font=('Microsoft YaHei UI',20,'bold')).grid(row=0,column=0,sticky='w')
        self.subtitle=ttk.Label(frame,text='原始强度与包络 · 选择条目查看说明 · 点击播放才震动')
        self.subtitle.grid(row=1,column=0,sticky='ew',pady=6)
        self.status=tk.StringVar(value='初始化手柄中…可以先浏览目录')
        status_label=ttk.Label(frame,textvariable=self.status);status_label.grid(row=2,column=0,sticky='ew')
        devicebar=ttk.Frame(frame);devicebar.grid(row=3,column=0,sticky='ew',pady=6);devicebar.columnconfigure(1,weight=1)
        ttk.Label(devicebar,text='回测设备：').grid(row=0,column=0)
        self.device_choice=ttk.Combobox(devicebar,state='readonly')
        self.device_choice.grid(row=0,column=1,sticky='ew',padx=8)
        self.device_choice.bind('<<ComboboxSelected>>',self.choose_device)
        ttk.Button(devicebar,text='刷新设备',command=self.refresh_devices).grid(row=0,column=2)
        mappingrow=ttk.Frame(frame);mappingrow.grid(row=4,column=0,sticky='ew',pady=4);mappingrow.columnconfigure(2,weight=1)
        ttk.Label(mappingrow,text='振幅映射：').grid(row=0,column=0)
        self.mapping=tk.StringVar(value=next(iter(PROFILES)))
        mapping_choice=ttk.Combobox(mappingrow,state='readonly',textvariable=self.mapping,values=list(PROFILES),width=26)
        mapping_choice.grid(row=0,column=1,padx=8)
        mapping_choice.bind('<<ComboboxSelected>>',self.mapping_changed)
        self.mapping_info=tk.StringVar(value='实验曲线尚未标定；仅用于 Switch 2 Pro 对比')
        ttk.Label(mappingrow,textvariable=self.mapping_info).grid(row=0,column=2,sticky='w')
        strengthrow=ttk.Frame(frame);strengthrow.grid(row=5,column=0,sticky='ew',pady=4);strengthrow.columnconfigure(1,weight=1)
        ttk.Label(strengthrow,text='震动强度：').grid(row=0,column=0)
        self.strength=tk.DoubleVar(value=50);self.strength_text=tk.StringVar(value='50%')
        self.strength_redraw=None
        ttk.Scale(strengthrow,from_=0,to=100,variable=self.strength,command=self.strength_changed).grid(row=0,column=1,sticky='ew',padx=8)
        ttk.Label(strengthrow,textvariable=self.strength_text,width=6).grid(row=0,column=2)
        ttk.Button(strengthrow,text='恢复 50%',command=self.reset_strength).grid(row=0,column=3,padx=8)
        ttk.Label(strengthrow,text='基准 ×10 · 10% 原始强度 · 100% 十倍请求').grid(row=0,column=4)
        self.search=tk.StringVar();self.search.trace_add('write',lambda *_:self.filter())
        searchrow=ttk.Frame(frame);searchrow.grid(row=6,column=0,sticky='ew',pady=8);searchrow.columnconfigure(1,weight=1)
        ttk.Label(searchrow,text='搜索名称 / 场景 / 资源：').grid(row=0,column=0)
        ttk.Entry(searchrow,textvariable=self.search).grid(row=0,column=1,sticky='ew')
        self.count=tk.StringVar();ttk.Label(searchrow,textvariable=self.count).grid(row=0,column=2,padx=8)
        paned=ttk.Panedwindow(frame,orient='horizontal');paned.grid(row=7,column=0,sticky='nsew')
        left=ttk.Frame(paned,width=int(width*.55));right=ttk.Frame(paned,width=int(width*.40))
        paned.add(left,weight=3);paned.add(right,weight=2)
        left.rowconfigure(0,weight=1);left.columnconfigure(0,weight=1)
        self.tree=ttk.Treeview(left,columns=('duration','peak'),show='tree headings',selectmode='browse',height=1)
        self.tree.heading('#0',text='效果（中文名称）');self.tree.heading('duration',text='时长ms');self.tree.heading('peak',text='振幅峰值')
        self.tree.column('#0',width=int(310*scale),minwidth=180,stretch=True)
        self.tree.column('duration',width=int(72*scale),minwidth=65,stretch=False)
        self.tree.column('peak',width=int(80*scale),minwidth=70,stretch=False)
        scroll=ttk.Scrollbar(left,command=self.tree.yview);self.tree.configure(yscrollcommand=scroll.set)
        self.tree.grid(row=0,column=0,sticky='nsew');scroll.grid(row=0,column=1,sticky='ns')
        self.tree.bind('<<TreeviewSelect>>',self.select)
        right.rowconfigure(0,weight=1);right.columnconfigure(0,weight=1)
        detailpanes=ttk.Panedwindow(right,orient='vertical');detailpanes.grid(row=0,column=0,sticky='nsew')
        plotframe=ttk.Frame(detailpanes);textframe=ttk.Frame(detailpanes)
        detailpanes.add(plotframe,weight=1);detailpanes.add(textframe,weight=3)
        self.plot=tk.Canvas(plotframe,height=1,width=1,bg='#151c29',highlightthickness=0)
        self.plot.pack(fill='both',expand=True);self.plot.bind('<Configure>',lambda e:self.draw())
        textframe.rowconfigure(0,weight=1);textframe.columnconfigure(0,weight=1)
        self.details=tk.Text(textframe,wrap='word',height=1,width=1,font=('Microsoft YaHei UI',10))
        ds=ttk.Scrollbar(textframe,command=self.details.yview);self.details.configure(yscrollcommand=ds.set)
        self.details.grid(row=0,column=0,sticky='nsew');ds.grid(row=0,column=1,sticky='ns')
        buttons=ttk.Frame(frame);buttons.grid(row=8,column=0,sticky='ew',pady=8)
        self.play=ttk.Button(buttons,text='▶ 播放选中效果',state='disabled',command=self.start);self.play.pack(side='left')
        ttk.Button(buttons,text='■ 停止 / Esc',command=self.stop).pack(side='left',padx=12)
        ttk.Button(buttons,text='打开效果目录',command=lambda:os.startfile(CATALOG.parent/'catalog.md')).pack(side='left')
        footer=ttk.Label(frame,text='场景标注仅采用已确认调用证据；未核实的剧情保留资源编号。波形不是手柄实测。')
        footer.grid(row=9,column=0,sticky='ew',pady=4)
        def fit(event):
            if event.widget==frame:
                status_label.configure(wraplength=max(100,event.width-20));footer.configure(wraplength=max(100,event.width-20))
        frame.bind('<Configure>',fit)
        # Tk can clamp sashpos to zero before the paned window is mapped. Set
        # the initial split only after a real allocation, and keep both panes
        # reachable when the window shrinks.
        split_ready=False
        def fit_split(event):
            nonlocal split_ready
            if event.width<200 or event.height<100:return
            minimum=min(int(240*scale),event.width//3)
            position=paned.sashpos(0) if split_ready else int(event.width*.55)
            paned.sashpos(0,max(minimum,min(event.width-minimum,position)))
            split_ready=True
        paned.bind('<Configure>',fit_split)
        root.bind('<Escape>',lambda e:self.stop());root.bind('<FocusOut>',lambda e:root.after(80,self.focus));root.protocol('WM_DELETE_WINDOW',self.close)
        self.filter();self.device=Device();self.poll()

    def record(self,item):
        with (CATALOG.parent/'backtest-results.jsonl').open('a',encoding='utf-8') as f:
            f.write(json.dumps(dict(timestamp=time.time(),**item),ensure_ascii=False)+'\n')

    def filter(self):
        if hasattr(self,'device'):self.stop()
        self.current=None
        self.tree.delete(*self.tree.get_children())
        needle=self.search.get().lower()
        count=0
        for i,e in enumerate(self.entries):
            if needle and needle not in (effect_name(e)+' '+confirmed_scenes(e,self.data)+' '+json.dumps({k:v for k,v in e.items() if k not in ('samples','pattern')},ensure_ascii=False)).lower():continue
            peak=max(row[1] for row in e['samples'])
            self.tree.insert('', 'end',iid=str(i),text=effect_name(e),values=(f"{e['duration_ms']:.0f}",f'{peak:.5f}'))
            count+=1
        self.count.set(f'{count} / {len(self.entries)} 项')
        self.details.configure(state='normal');self.details.delete('1.0','end');self.details.insert('end','请选择左侧效果，查看本项测试内容和场景证据。');self.details.configure(state='disabled');self.draw()

    def select(self,event=None):
        self.stop();selection=self.tree.selection()
        if not selection:return
        self.current=self.entries[int(selection[0])]
        e=self.current;details={k:v for k,v in e.items() if k not in ('samples','pattern')}
        self.details.configure(state='normal');self.details.delete('1.0','end');self.details.insert('end',effect_description(e,self.data));self.details.configure(state='disabled');self.draw()

    def draw(self):
        self.plot.delete('all')
        if not self.current:return
        mapped=map_samples(self.current['samples'],PROFILES[self.mapping.get()],self.strength_percent())
        peak=max(max(r[i] for i in (1,3,5,7)) for r in self.current['samples'])
        output=max(max(r[i] for i in (1,3,5,7)) for r in mapped)
        self.mapping_info.set(f'峰值 {peak:.5f} → {output:.5f}；实验曲线未标定')
        samples=self.current['samples'];w,h=self.plot.winfo_width(),self.plot.winfo_height();duration=max(1,samples[-1][0]);points=[]
        for row in samples:points.extend((10+(w-20)*row[0]/duration,h-20-(h-40)*row[1]))
        if len(points)>=4:self.plot.create_line(*points,fill='#b49aff',width=2)
        points=[]
        for row in mapped:points.extend((10+(w-20)*row[0]/duration,h-20-(h-40)*row[1]))
        if len(points)>=4:self.plot.create_line(*points,fill='#52dbbf',width=2,dash=(4,3))
        self.plot.create_text(12,12,anchor='nw',fill='white',text='低频振幅：紫=原始，绿虚线=输出（0–1）')

    def strength_percent(self):
        return max(0,min(100,round(self.strength.get())))

    def strength_changed(self,value=None):
        self.strength_text.set(f'{self.strength_percent()}%')
        self.stop()
        self.status.set('强度已调整；点击播放测试。0% 静音，10% 原始强度，100% 十倍请求。')
        if self.strength_redraw is None:
            self.strength_redraw=self.root.after(16,self.redraw_strength)

    def redraw_strength(self):
        self.strength_redraw=None;self.draw()

    def reset_strength(self):
        self.strength.set(50);self.strength_changed()

    def mapping_changed(self,event=None):
        self.stop();self.draw()
        self.status.set('映射已切换；点击播放对比。实验曲线不是已标定的原版震感。')

    def start(self):
        if self.ready and self.current:
            device=next((d for d in self.devices if d['id']==self.selected_device),None)
            drive=PROFILES[self.mapping.get()]
            if drive!=1 and (not device or (device['vendor'],device['product'])!=(0x057e,0x2069)):
                self.status.set('实验增强仅用于 Switch 2 Pro；其他设备请选择原始映射。');return
            entry=dict(self.current,samples=map_samples(self.current['samples'],drive,self.strength_percent()))
            validate_samples(entry['samples'])
            self.device.commands.put(('play',{'entry':entry,'device_id':self.selected_device,'mapping':self.mapping.get(),'drive':drive,'strength_percent':self.strength_percent(),'base_strength_gain':BASE_STRENGTH_GAIN}))
            self.status.set('正在回放：'+effect_name(self.current)+' · '+self.mapping.get()+f' · {self.strength_percent()}%')
            self.record({'event':'start','id':self.current['id'],'device':device,'mapping':self.mapping.get(),'drive':drive,'strength_percent':self.strength_percent(),'base_strength_gain':BASE_STRENGTH_GAIN})

    def refresh_devices(self):
        self.ready=False;self.play.configure(state='disabled');self.status.set('正在刷新设备…')
        self.device.commands.put(('refresh',None))

    def choose_device(self,event=None):
        index=self.device_choice.current()
        if index<0:return
        self.ready=False;self.play.configure(state='disabled');self.status.set('正在切换设备…')
        self.device.commands.put(('select',self.devices[index]['id']))

    def stop(self):
        if hasattr(self,'device'):self.device.commands.put(('stop',None))

    def focus(self):
        if self.root.focus_displayof() is None:self.stop()

    def poll(self):
        while not self.device.messages.empty():
            kind,message=self.device.messages.get_nowait()
            if kind=='devices':
                self.devices=message;self.device_choice.configure(values=[d['label'] for d in message])
            elif kind=='selection':
                self.selected_device=message
                index=next((i for i,d in enumerate(self.devices) if d['id']==message),None)
                if index is None:self.device_choice.set('请选择手柄' if self.devices else '未发现手柄')
                else:self.device_choice.current(index)
            elif kind=='ready':self.ready=True;self.play.configure(state='normal');self.status.set(message)
            elif kind=='error':self.ready=False;self.play.configure(state='disabled');self.status.set(message)
            elif kind=='finished':self.record(dict(event='finish',**message));self.status.set('已停止；API最大迟到 %.2f ms（非USB实测）'%message['max_api_late_ms'])
        self.root.after(50,self.poll)

    def close(self):
        self.stop();self.device.quit.set();self.root.withdraw()
        def finish():
            if self.device.worker.is_alive():self.root.after(50,finish)
            else:self.root.destroy()
        finish()


if __name__=='__main__':
    try:C.windll.user32.SetProcessDpiAwarenessContext(C.c_void_p(-4))
    except Exception:pass
    root=tk.Tk()
    try:Player(root)
    except Exception as exc:
        from tkinter import messagebox
        messagebox.showerror('回测工具',str(exc));root.destroy()
    else:root.mainloop()
