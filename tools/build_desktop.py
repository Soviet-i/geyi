"""Build native LVGL desktop simulator using the installed SDK, not a copied rules engine."""
from pathlib import Path
import concurrent.futures
import hashlib
import os
import shutil
import subprocess
import sys
sys.stdout.reconfigure(encoding='utf-8',errors='replace')

root=Path(__file__).resolve().parents[1]
_sdk=[]
if os.environ.get('SIFLI_SDK'):_sdk.append(Path(os.environ['SIFLI_SDK']))
_sdk+=[Path(r'D:\233\SiFli-SDK\v2.5.1'),Path(r'D:\sifli\SiFli-SDK')]
sdk=next((p for p in _sdk if (p/'external/lvgl_v9/lvgl.h').exists()),_sdk[-1])
lvgl=sdk/'external/lvgl_v9'
out=root/'build/desktop'
out.mkdir(parents=True,exist_ok=True)
logs=[]
executable=out/'geyi-desktop-cn.exe'
candidates=[]
if os.environ.get('VS_ROOT'):
    candidates.append(Path(os.environ['VS_ROOT']))
vswhere=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
if vswhere.exists():
    probe=subprocess.run([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],capture_output=True,text=True)
    for line in probe.stdout.splitlines():
        if line.strip(): candidates.append(Path(line.strip()))
candidates += [Path(os.environ.get('ProgramFiles',r'C:\Program Files'))/'Microsoft Visual Studio/2022',
               Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/2022']
vcvars=None
for candidate in candidates:
    found=next(candidate.glob('*/VC/Auxiliary/Build/vcvars64.bat'),None) if candidate.is_dir() else None
    if not found and (candidate/'VC/Auxiliary/Build/vcvars64.bat').exists():
        found=candidate/'VC/Auxiliary/Build/vcvars64.bat'
    if found:
        vcvars=found; break
if not (lvgl/'lvgl.h').exists():raise SystemExit('SDK LVGL v9 not found')
app=[root/'src/geyi_game.c',root/'src/geyi_feedback.c',root/'src/geyi_ai.c',
     root/'src/geyi_ai_task.c',root/'src/geyi_config.c',root/'src/geyi_param_ui.c',
     root/'src/geyi_net_proto.c',root/'src/geyi_net_room.c',root/'src/geyi_net_link.c',
     root/'src/geyi_net_app.c',root/'src/geyi_nav.c',
     root/'src/geyi_ui.c',root/'src/geyi_font_cn16.c',root/'tools/geyi_desktop.c']
if not vcvars:
    # 没装 VS2022 时用 gcc/clang 回退；Windows 桌面驱动仍用 Win32 GDI，不引入 SDL。
    gcc=shutil.which('gcc') or shutil.which('clang')
    if not gcc:raise SystemExit('MSVC/gcc/clang not found')
    flags=['-std=c11','-O1','-DLV_KCONFIG_IGNORE','-DLV_CONF_INCLUDE_SIMPLE',
           '-I'+str(root/'tools/desktop'),'-I'+str(root/'src'),'-I'+str(lvgl)]
    def gcc_obj(p,extra=()):
        obj=out/(hashlib.sha256(str(p).encode()).hexdigest()[:16]+'.o')
        if not obj.exists() or obj.stat().st_mtime<p.stat().st_mtime:
            r=subprocess.run([gcc,*flags,*extra,'-c',str(p),'-o',str(obj)],cwd=out,capture_output=True,text=True)
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return obj
    try:
        sources=sorted((lvgl/'src').rglob('*.c'))
        print('Compiling',len(sources),'SDK LVGL sources with',gcc,'...',flush=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            objs=list(pool.map(gcc_obj,sources))
        objs+=[gcc_obj(p,('-DGEYI_WITH_LVGL',)) for p in app]
        r=subprocess.run([gcc,*[str(o) for o in objs],'-o',str(executable),
                          '-luser32','-lgdi32','-mwindows'],cwd=out,capture_output=True,text=True)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        r=subprocess.run([str(executable),'--test'],cwd=out,capture_output=True,timeout=60)
        logs.append(r.stdout+r.stderr);print(logs[-1])
        if r.returncode:raise RuntimeError('UI tests failed, exit '+str(r.returncode))
    except Exception as exc:
        logs.append(str(exc));print(str(exc));(out/'desktop-test.log').write_text('\n'.join(logs),encoding='utf-8');raise SystemExit(1)
    (out/'desktop-test.log').write_text('\n'.join(logs),encoding='utf-8')
    print('Ready:',executable);raise SystemExit(0)
result=subprocess.run('cmd.exe /d /s /c "call "'+str(vcvars)+'" >nul && set"',capture_output=True)
if result.returncode:raise SystemExit(result.stderr.decode(errors='replace'))
env=os.environ.copy()
for line in result.stdout.decode('mbcs',errors='replace').splitlines():
    if '=' in line:
        key,value=line.split('=',1)
        if key:env[key]=value
temporary=out/'temp'
temporary.mkdir(exist_ok=True)
env['TEMP']=env['TMP']=str(temporary)
cc=shutil.which('cl.exe',path=env.get('Path',env.get('PATH')))
lib=shutil.which('lib.exe',path=env.get('Path',env.get('PATH')))
flags=['/nologo','/std:c11','/utf-8','/O1','/DLV_KCONFIG_IGNORE','/DLV_CONF_INCLUDE_SIMPLE',
       '/DGEYI_WITH_LVGL','/I'+str(root/'tools/desktop'),'/I'+str(root/'src'),'/I'+str(lvgl)]
newest=max(p.stat().st_mtime for p in (root/'src').glob('*.h'))
newest=max(newest,(root/'tools/desktop/lv_conf.h').stat().st_mtime)
sources=sorted((lvgl/'src').rglob('*.c'))
names=[str(p) for p in sources]
def compile_one(p):
    obj=out/(hashlib.sha256(str(p).encode()).hexdigest()[:16]+'.obj')
    if not obj.exists() or obj.stat().st_mtime<max(newest,p.stat().st_mtime):
        r=subprocess.run([cc,*flags,'/c',str(p),'/Fo'+str(obj)],cwd=out,env=env,capture_output=True)
        if r.returncode:raise RuntimeError(r.stdout.decode('mbcs',errors='replace')+r.stderr.decode('mbcs',errors='replace'))
    return obj
try:
    print('Compiling',len(sources),'SDK LVGL sources...',flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:objs=list(pool.map(compile_one,sources))
    response=out/'lvgl-objects.rsp'
    response.write_text('\n'.join('"'+str(p)+'"' for p in objs),encoding='utf-8-sig')
    r=subprocess.run([lib,'/nologo','/OUT:'+str(out/'lvgl.lib'),'@'+str(response)],cwd=out,env=env,capture_output=True)
    if r.returncode:raise RuntimeError(r.stdout.decode('mbcs',errors='replace'))
    app_objects=[compile_one(p) for p in app]
    command=[cc,'/nologo',*[str(p) for p in app_objects],str(out/'lvgl.lib'),
             '/Fe:'+str(executable),'/link','/SUBSYSTEM:WINDOWS','user32.lib','gdi32.lib']
    r=subprocess.run(command,cwd=out,env=env,capture_output=True)
    if r.returncode:raise RuntimeError(r.stdout.decode('mbcs',errors='replace')+r.stderr.decode('mbcs',errors='replace'))
    r=subprocess.run([str(executable),'--test'],cwd=out,capture_output=True,timeout=30)
    logs.append(r.stdout.decode('utf-8',errors='replace')+r.stderr.decode('utf-8',errors='replace'))
    print(logs[-1])
    if r.returncode:raise RuntimeError('UI tests failed, exit '+str(r.returncode))
except Exception as exc:
    logs.append(str(exc));print(str(exc));(out/'desktop-test.log').write_text('\n'.join(logs),encoding='utf-8');raise SystemExit(1)
(out/'desktop-test.log').write_text('\n'.join(logs),encoding='utf-8')
print('Ready:',executable)
