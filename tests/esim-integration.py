#!/usr/bin/env python3
"""Isolated Linux integration: shipped C -> coordinator -> Rust -> QMI/APDU.

Run inside a disposable arm64 container with /work mounted read-only. The
modem and lpac are stand-ins; no hardware or external network is reachable.
The real lpac's stdio protocol is checked separately by lpac-stdio.py.
"""
import json, os, pathlib, shutil, socket, struct, subprocess, threading, time
R=pathlib.Path('/work')
assert pathlib.Path('/.dockerenv').exists() and R.is_dir(), 'Run only in the documented disposable container'
D=pathlib.Path('/data/u60-panel');D.mkdir(parents=True)
shutil.copy(R/'build/panel-control',D/'panel-control')
shutil.copy(R/'esim-agent/target/aarch64-unknown-linux-musl/debug/u60-esim',D/'u60-esim')
script=(R/'panel/esim-control.sh').read_text().replace('umask 077','umask 077\nexport OPENUI_QMI_SOCKET_DIR=/tmp/modem')
(D/'esim-control.sh').write_text(script);(D/'esim-control.sh').chmod(0o700)
Q=pathlib.Path('/tmp/modem');Q.mkdir()
EID='89049032'+'0'*24
A='89444765'+'00001234567';B='89852000'+'000000000012';NEW='89012345'+'000000000099'
model={'eid':EID,'card':'euicc','mode':0,'fw':'BD_CNMU5250V1.0.0B31','profiles':[{'iccid':A,'profileName':'Alpha','profileNickname':'','profileState':'enabled'},{'iccid':B,'profileName':'Beta','profileNickname':'','profileState':'disabled'}],'notifications':[], 'busy':False, 'slow':False}
channels=set();events=[]
def tlv(k,v):return bytes([k])+struct.pack('<H',len(v))+v
def fields(b):
 out={};i=0
 while i<len(b):k=b[i];n=struct.unpack_from('<H',b,i+1)[0];out[k]=b[i+3:i+3+n];i+=3+n
 return out
def selected():return next((p['iccid'] for p in model['profiles'] if p['profileState']=='enabled'),'')
def operate(args):
 if args[:2]==['chip','info']:return {'eidValue':model['eid']}
 if args[:2]==['profile','list']:return model['profiles']
 if args[:2]==['notification','list']:return model['notifications']
 if args[:2]==['profile','enable']:
  if model['busy']:raise ValueError('catBusy')
  for p in model['profiles']:p['profileState']='enabled' if p['iccid']==args[2] else 'disabled'
  events.append('enable');return None
 if args[:2]==['profile','disable']:
  for p in model['profiles']:
   if p['iccid']==args[2]:p['profileState']='disabled'
  events.append('disable');return None
 if args[:2]==['profile','nickname']:
  for p in model['profiles']:
   if p['iccid']==args[2]:p['profileNickname']=args[3]
  return None
 if args[:2]==['profile','delete']:
  model['profiles']=[p for p in model['profiles'] if p['iccid']!=args[2]];events.append('delete');return None
 if args[:2]==['profile','download']:
  if model['slow']:time.sleep(6)
  model['profiles'].append({'iccid':NEW,'profileName':'Travel','profileNickname':'','profileState':'disabled'})
  events.append('download');return {'iccid':NEW}
 if args[0]=='notification':return None
 raise ValueError('unsupported')
def serve(service):
 s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);s.bind(str(Q/f'qmi-{service}.sock'))
 while True:
  raw,peer=s.recvfrom(16384);_,txn,msg,n=struct.unpack_from('<BHHH',raw);fs=fields(raw[7:7+n]);parts=[tlv(2,b'\0\0\0\0')]
  if service==2:
   if msg==0x2d:parts.append(tlv(1,bytes([model['mode']])))
   if msg==0x2e:model['mode']=fs[1][0];events.append('low-power' if model['mode']==1 else 'online')
  elif msg==0x2f:
   v=bytes([0,0,255,255,255,255,255,255,1,1 if model['card']!='absent' else 0,0,3,10,0,1,2,7,0,0,0,0,0,0,1,3,10,0,3,10]);parts.append(tlv(0x10,v))
  elif msg==0x42:
   if model['card']=='sim':parts=[tlv(2,struct.pack('<HH',1,80))]
   else:channels.add((peer,2));parts.extend([tlv(0x10,b'\x02'),tlv(0x11,b'\x90\x00')])
  elif msg==0x3f:channels.discard((peer,fs[0x11][0]))
  elif msg==0x3b:
   args=json.loads(fs[2][2:].decode())
   try:data={'ok':True,'data':operate(args)}
   except ValueError as e:data={'ok':False,'error':str(e)}
   payload=json.dumps(data).encode()+b'\x90\x00';parts.append(tlv(0x10,struct.pack('<H',len(payload))+payload))
  elif msg in (0x30,0x31):events.append('power-on' if msg==0x31 else 'power-off');pathlib.Path('/tmp/firmware-iccid').write_text(selected() if msg==0x31 else '')
  elif msg==0x47:
   iccid=selected();padded=iccid+('F' if len(iccid)%2 else '');bcd=bytes(int(padded[i+1]+padded[i],16) for i in range(0,len(padded),2));parts.append(tlv(0x10,b'\x01'+struct.pack('<IIB',2,1,1)+bytes([len(bcd)])+bcd))
  body=b''.join(parts)
  try:s.sendto(struct.pack('<BHHH',2,txn,msg,len(body))+body,peer)
  except FileNotFoundError:pass
for svc in (2,11):threading.Thread(target=serve,args=(svc,),daemon=True).start()
(D/'lpac').write_text('''#!/usr/local/bin/python3
import sys,json
def req(func,param):
 print(json.dumps({'type':'apdu','payload':{'func':func,'param':param}}),flush=True);return json.loads(input())['payload']
req('connect',None);ch=req('logic_channel_open','a0000005591010ffffffff8900000100')['ecode']
reply=req('transmit',json.dumps(sys.argv[1:]).encode().hex());data=json.loads(bytes.fromhex(reply['data'])[:-2])
print(json.dumps({'type':'lpa','payload':{'code':0 if data['ok'] else -1,'message':'success' if data['ok'] else 'es10c_enable_profile','data':data.get('data') if data['ok'] else data.get('error')}}),flush=True)
req('logic_channel_close',bytes([ch]).hex());req('disconnect',None)
''');(D/'lpac').chmod(0o700)
pathlib.Path('/tmp/firmware-iccid').write_text(A)
ubus=pathlib.Path('/usr/bin/ubus');ubus.write_text('''#!/usr/local/bin/python3
import json,sys,pathlib
if 'get_zwrt_common_info' in sys.argv:print(json.dumps({'wa_inner_version':pathlib.Path('/tmp/firmware').read_text()}))
elif 'get_sim_info' in sys.argv:print(json.dumps({'sim_iccid':pathlib.Path('/tmp/firmware-iccid').read_text(),'sim_states':'modem_init_complete'}))
else:print('{}')
''');ubus.chmod(0o700);pathlib.Path('/tmp/firmware').write_text(model['fw'])
# BusyBox jsonfilter's sole use in the coordinator: detect unfinished jobs.
j=pathlib.Path('/usr/bin/jsonfilter');j.write_text("#!/usr/local/bin/python3\nimport sys,json\nprint(json.load(open(sys.argv[2])).get('job',{}).get('state',''))\n");j.chmod(0o700)
def call(action,args=None):
 out=subprocess.run([str(D/'panel-control')],input=json.dumps({'action':action,'args':args or {}}),capture_output=True,text=True,timeout=15)
 return json.loads(out.stdout)
def state():return call('esim.state')['esim']
def wait():
 until=time.monotonic()+20
 while time.monotonic()<until:
  v=state()
  if v.get('job',{}).get('state')!='running':return v
  time.sleep(.15)
 raise AssertionError('worker did not finish')
def act(kind,**kw):
 assert call('esim.'+kind,{'expected_eid':EID,'confirmed':True} | kw)['ok'];return wait()
assert call('esim.scan')['ok'];v=wait();assert v['view']['card']=='euicc' and len(v['view']['profiles'])==2,v
v=act('nickname',iccid=B,nickname='出行');assert v['job']['state']=='done' and v['view']['profiles'][1]['nickname']=='出行',v
v=act('enable',iccid=B);assert v['job']['result']['firmware_ready'] and selected()==B,v
assert events[-5:]==['enable','low-power','power-off','power-on','online'],events
model['busy']=True;before=list(events);v=act('enable',iccid=A);assert v['job']['state']=='failed' and 'catBusy' in v['job']['error'] and events==before,v;model['busy']=False
before=len(events);v=act('enable',iccid=A,expected_eid='0'*32);assert v['job']['state']=='failed' and len(events)==before,v
# Duplicate submission is refused while the worker holds the eSIM lock.
model['slow']=True;assert call('esim.download',{'expected_eid':EID,'confirmed':True,'activation_code':'LPA:1$smdp.example$SYNTHETIC'})['ok']
assert not call('esim.scan')['ok'];v=wait();assert v['job']['state']=='done' and any(p['iccid']==NEW and not p['enabled'] for p in v['view']['profiles']),v
assert not call('esim.delete',{'iccid':NEW,'expected_eid':EID})['ok']
v=act('delete',iccid=NEW,confirm_name='Wrong');assert v['job']['state']=='failed' and any(p['iccid']==NEW for p in v['view']['profiles']),v
v=act('delete',iccid=NEW,confirm_name='Travel');assert v['job']['state']=='done' and not any(p['iccid']==NEW for p in v['view']['profiles']),v
# A firmware lag hint must survive a new worker and clear only after readback.
saved=state();saved['reboot_needed']=True;saved['reboot_target']=A
pathlib.Path('/tmp/u60-esim/state.json').write_text(json.dumps(saved))
assert call('esim.scan')['ok'];assert wait()['reboot_needed']
pathlib.Path('/tmp/firmware-iccid').write_text(A)
assert call('esim.scan')['ok'];assert not wait()['reboot_needed']
# A crashed worker does not leave a fabricated successful job.
saved=state();saved['job']={'state':'running','step':'writing'}
pathlib.Path('/tmp/u60-esim/state.json').write_text(json.dumps(saved))
assert state()['job']['state']=='interrupted'
model['card']='sim';assert call('esim.scan')['ok'];assert wait()['view']['card']=='sim'
model['card']='absent';assert call('esim.scan')['ok'];assert wait()['view']['card']=='absent'
pathlib.Path('/tmp/firmware').write_text('BD_FLYMODEMMU5250V1.0.0B28');assert call('esim.scan')['ok'];assert 'B31' in wait()['job']['error']
assert not channels,channels
assert 'SYNTHETIC' not in pathlib.Path('/tmp/u60-esim/state.json').read_text()
print('PASS: C/worker/QMI/APDU lifecycle, switch/radio recovery, eSTK catBusy, card binding, download/delete confirmations, duplicate refusal, ordinary/empty SIM, firmware guard, channel closure')
