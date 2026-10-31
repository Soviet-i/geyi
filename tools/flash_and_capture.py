"""Student-operated board bring-up. Requires explicit interactive FLASH confirmation."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import serial
import serial.tools.list_ports

sys.stdout.reconfigure(encoding='utf-8',errors='replace')
root=Path(__file__).resolve().parents[1]
build=root/'project/build_sf32lb52-lchspi-ulp_hcpu'
def find_sftool():
    candidates=[os.environ.get('SFTOOL'),shutil.which('sftool')]
    candidates+=sorted((str(p) for p in (Path.home()/'.sifli/tools/sftool').glob('*/sftool.exe')),reverse=True)
    for candidate in candidates:
        if candidate and Path(candidate).is_file():return Path(candidate)
    raise SystemExit('sftool missing; set SFTOOL to the sftool executable path.')
tool=find_sftool()
ports=[p.device for p in serial.tools.list_ports.comports() if (p.vid,p.pid)==(0x1a86,0x7523)]
if len(ports)!=1:raise SystemExit('Exactly one CH340 board is required; do not guess COM ports.')
if not tool.exists() or not (build/'sftool_param.json').exists():raise SystemExit('Build or sftool missing.')
binary=build/'main.bin'
sources=list((root/'src').glob('*.c'))+list((root/'src').glob('*.h'))+[root/'project/proj.conf',root/'src/SConscript']
if not binary.exists() or max(p.stat().st_mtime for p in sources)>binary.stat().st_mtime:
    raise SystemExit('Sources changed since firmware build. Rebuild successfully before flashing.')
print('Required board: sf32lb52-lchspi-ulp. Cancel if the model is different.')
print('Port:',ports[0],'. Flash replaces board firmware; PC projects remain unchanged.')
if '--check' in sys.argv:
    print('READ-ONLY preflight passed; no serial open, flashing or reboot performed.')
    raise SystemExit(0)
print('Close other serial monitors. Put the board flat and keep USB connected.')
print('After calibration: test touch, then tilt UP/RIGHT/DOWN/LEFT and SHORT-tap KEY1.')
print('KEY1 is power-multiplexed: DO NOT hold it. KEY2 cancels or enters placement.')
if input('Type FLASH to flash and capture 120 seconds; anything else cancels: ').strip()!='FLASH':raise SystemExit('Cancelled')
stamp=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).strftime('%Y%m%d_%H%M%S')
folder=root/'evidence/board'/stamp
folder.mkdir(parents=True,exist_ok=False)
files=list(build.glob('*.bin'))+list((build/'bootloader').glob('*.bin'))+list((build/'ftab').glob('*.bin'))
metadata={'board':'sf32lb52-lchspi-ulp','port':ports[0],'time':stamp,
          'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
          'worktree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=root)),
          'source_files':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
          'files':{str(p.relative_to(build)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
(folder/'firmware.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf-8')
command=[str(tool),'-p',ports[0],'-b','1000000','config','sftool_param.json']
with (folder/'flash.log').open('wb') as log:
    process=subprocess.Popen(command,cwd=build,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    for line in iter(process.stdout.readline,b''):
        log.write(line);log.flush();print(line.decode('utf-8',errors='replace'),end='',flush=True)
    code=process.wait()
if code:raise SystemExit(f'Flash failed ({code}); keep logs: {folder}')
print('\nFlash returned success. This does NOT verify the screen or controls.')
device=serial.Serial();device.port=ports[0];device.baudrate=1000000
device.timeout=0.2;device.dtr=False;device.rts=False
received=bytearray()
try:
    device.open()
    with (folder/'serial.log').open('wb') as log:
        device.write(b'\r\n');device.flush();time.sleep(0.3)
        device.write(b'reboot\r\n');device.flush()
        end=time.monotonic()+120
        while time.monotonic()<end:
            data=device.read(device.in_waiting or 1)
            if data:
                received.extend(data);log.write(data);log.flush()
                print(data.decode('utf-8',errors='replace'),end='',flush=True)
except serial.SerialException as exc:
    print('\nSerial error:',exc,'. Keep the existing logs; do not assume test passed.')
finally:device.close()
print('\nEvidence folder:',folder)
markers=[b'core=PASS',b'input=PASS',b'[geyi:lcd] READY',b'WHO_AM_I=6a',b'[geyi:imu] calibrated']
for marker in markers:print(marker.decode(),':','CAPTURED' if marker in received else 'MISSING')
print('Send screen photo, terminal output and your observed four-direction/key behavior.')
print('Captured logs do not prove correct hardware behavior. No Git commit/upload was made.')
