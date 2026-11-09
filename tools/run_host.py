"""Build actual C core and CLI; no Python copy of the rules. Requires MSVC or CC."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'build' / ('host-verify' if '--verify' in sys.argv else 'host')
out.mkdir(parents=True, exist_ok=True)
logs = []
# --only=NAME：只编译/运行一个 target，用于输入/倾斜等局部回归，不连带联机套件。
only = None
_args = sys.argv[1:]
for _i, _a in enumerate(_args):
    if _a.startswith('--only='):
        only = _a.split('=', 1)[1]
    elif _a == '--only' and _i + 1 < len(_args):
        only = _args[_i + 1]

def run(args, **kw):
    result = subprocess.run(args, cwd=out, text=True, encoding='utf-8', errors='replace',
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    print(result.stdout)
    logs.append(result.stdout)
    if result.returncode:
        (out/'host_test.log').write_text('\n'.join(logs), encoding='utf-8')
        raise SystemExit(result.returncode)
    return result.stdout

# New Chinese UI literals must be added to the generated CN16 subset first.
font_check = subprocess.run([sys.executable, str(root/'tools'/'check_desktop_font.py')],
                            cwd=root, text=True, encoding='utf-8', errors='replace',
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(font_check.stdout)
if font_check.returncode:
    raise SystemExit(font_check.returncode)

cc = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
targets = [
    ('geyi-tests', ['src/geyi_game.c', 'src/geyi_input.c', 'tests/test_geyi.c']),
    ('geyi-input-tests', ['src/geyi_game.c', 'src/geyi_input.c', 'tests/test_input.c']),
    ('geyi-tilt-ux-tests', ['src/geyi_game.c', 'src/geyi_input.c', 'src/geyi_nav.c',
                            'tests/test_tilt_ux.c']),
    ('geyi-config-tests', ['src/geyi_game.c', 'src/geyi_input.c', 'src/geyi_config.c', 'tests/test_config.c']),
    ('geyi-param-ui-tests', ['src/geyi_game.c', 'src/geyi_config.c', 'src/geyi_param_ui.c', 'tests/test_param_ui.c']),
    ('geyi-ai-tests', ['src/geyi_game.c', 'src/geyi_ai.c', 'tests/test_ai.c']),
    ('geyi-net-tests', ['src/geyi_game.c', 'src/geyi_net_proto.c', 'src/geyi_net_room.c',
                        'src/geyi_net_link.c', 'tests/test_net.c']),
    ('geyi-net-app-tests', ['src/geyi_game.c', 'src/geyi_config.c',
                            'src/geyi_net_proto.c', 'src/geyi_net_room.c',
                            'src/geyi_net_link.c', 'tests/test_net_app.c']),
    ('geyi-balance-tests', ['src/geyi_game.c', 'tools/geyi_balance.c', 'tests/test_balance.c']),
    ('geyi-balance-scan', ['src/geyi_game.c', 'tools/geyi_balance.c', 'tools/geyi_balance_scan.c']),
    ('geyi-ai-bench', ['src/geyi_game.c', 'src/geyi_ai.c', 'tools/geyi_ai_bench.c']),
    ('geyi-input-bench', ['src/geyi_game.c', 'src/geyi_input.c', 'tools/geyi_input_bench.c']),
    ('geyi-cli', ['src/geyi_game.c', 'src/geyi_input.c', 'tools/geyi_cli.c']),
]
for name, relative_sources in targets:
    if only and name != only:
        continue
    exe = out/(name + ('.exe' if os.name=='nt' else ''))
    sources = [str(root/source) for source in relative_sources]
    if cc:
        run([cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(root/'src'),'-I'+str(root/'tools'),*sources,'-o',str(exe)])
    else:
        base = Path(os.environ.get('ProgramFiles', r'C:\Program Files'))/'Microsoft Visual Studio'/'2022'
        scripts = list(base.glob('*/VC/Auxiliary/Build/vcvars64.bat'))
        if not scripts: raise SystemExit('MSVC/gcc/clang not found; no tests were run.')
        command = f'call "{scripts[0]}" >nul && cl /nologo /std:c11 /W4 /WX /utf-8 /I"{root / "src"}" /I"{root / "tools"}" ' + ' '.join(f'"{s}"' for s in sources) + f' /Fe:"{exe}"'
        run('cmd.exe /d /s /c "' + command + '"')
    if name.endswith('tests'): run([str(exe)],timeout=30)

if only:
    if only not in [n for n, _ in targets]:
        raise SystemExit(f'unknown --only target: {only}')
    (out/'host_test.log').write_text('\n'.join(logs), encoding='utf-8')
    print(f'ONLY {only}: done')
    raise SystemExit(0)

smoke = run([str(out/'geyi-cli.exe' if os.name=='nt' else out/'geyi-cli')],
    input='w 1\ncancel\nw 1\nok\nresign\nrestart\nquit\n',timeout=10)
assert 'COMMITTED' in smoke and 'Turn=BLUE' in smoke and 'PREVIEW ONLY' in smoke, 'CLI smoke behavior failed'
(out/'host_test.log').write_text('\n'.join(logs), encoding='utf-8')
print('CLI ready:',out/'geyi-cli.exe')
if '--play' in sys.argv:
    subprocess.run([str(out/('geyi-cli.exe' if os.name=='nt' else 'geyi-cli'))],cwd=root)
