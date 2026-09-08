from pathlib import Path
import argparse
import fcntl
import json
import sys
import time
import uuid

sys.path.insert(0,'tools')
import vita_fps_benchmark as b
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--original',type=Path,required=True)
parser.add_argument('--host',default='192.168.1.146')
parser.add_argument('--package',type=Path,required=True)
args=parser.parse_args()
out=args.output
out.mkdir(exist_ok=False)
original=b.package_payload(args.original)
payload=b.package_payload(args.package)
folder='rt64-architecture'
marker=b'PASS: GXM packet architecture control'
remote='/ux0:/data/'+folder
passed=False
with Path('build/fps-autobench/device.lock').open('a') as lock:
    fcntl.flock(lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
    with b.Device(args.host) as device:
        if device.get(b.EBOOT)!=original:raise ValueError('Unexpected installed executable')
        before=device.core_inventory()
        b.write_json(out/'core-dumps-before.json',before)
        saves={n:device.get(b.SAVES+'/'+n) for n in device.list(b.SAVES) if n not in ('.','..')}
        savedir=out/'normal-saves-before';savedir.mkdir()
        for name,value in saves.items():(savedir/name).write_bytes(value)
        device.mkdir(remote)
        for filename in ('results.log','error.log'):
            if device.exists(remote+'/'+filename):
                (out/('previous-'+filename)).write_bytes(device.get(remote+'/'+filename))
                device.rename(remote+'/'+filename,remote+'/'+filename+'.before-'+uuid.uuid4().hex[:12])
        try:
            with b.temporary_executable(device,payload,original,out):
                device.launch()
                deadline=time.monotonic()+150
                while time.monotonic()<deadline:
                    if device.exists(remote+'/results.log'):
                        value=device.get(remote+'/results.log')
                        (out/'results.log').write_bytes(value)
                        if b'FAIL' in value:raise ValueError('Native control reported a failure')
                        if marker in value:
                            passed=True
                            time.sleep(10)
                            break
                    time.sleep(2)
                else:raise TimeoutError('Native control missed its deadline')
        finally:
            for filename in ('results.log','error.log'):
                if device.exists(remote+'/'+filename):(out/filename).write_bytes(device.get(remote+'/'+filename))
            after=device.core_inventory()
            b.write_json(out/'core-dumps-after.json',after)
            changed=[n for n,v in after.items() if before.get(n)!=v]
            for name in changed:
                (out/'coredumps').mkdir(exist_ok=True)
                (out/'coredumps'/name).write_bytes(device.get('/ux0:/data/'+name,limit=256*1024*1024))
            saved={n:device.get(b.SAVES+'/'+n) for n in device.list(b.SAVES) if n not in ('.','..')}
            record={'control_passed':passed,'new_core_dumps':changed,'normal_saves_unchanged':saved==saves,
                'original_restored':device.get(b.EBOOT)==original,'vpk_sha256':b.digest(args.package.read_bytes()),
                'eboot_sha256':b.digest(payload),'normal_saves_after':{n:b.digest(v) for n,v in saved.items()}}
            b.write_json(out/'validation.json',record)
            print(json.dumps(record,indent=2),flush=True)
            if changed or saved!=saves or not record['original_restored']:raise RuntimeError('Device validation failed')
print((out/'results.log').read_text(),flush=True)
