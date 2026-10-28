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

def run(args, **kw):
    result = subprocess.run(args, cwd=out, text=True, encoding='utf-8', errors='replace',
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    print(result.stdout)
    logs.append(result.stdout)
    if result.returncode:
        (out/'host_test.log').write_text('\n'.join(logs), encoding='utf-8')
        raise SystemExit(result.returncode)
    return result.stdout

cc = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
targets = [
    ('geyi-tests', ['src/geyi_game.c', 'src/geyi_input.c', 'tests/test_geyi.c']),
    ('geyi-input-tests', ['src/geyi_game.c', 'src/geyi_input.c', 'tests/test_input.c']),
    ('geyi-config-tests', ['src/geyi_game.c', 'src/geyi_input.c', 'src/geyi_config.c', 'tests/test_config.c']),
    ('geyi-param-ui-tests', ['src/geyi_game.c', 'src/geyi_config.c', 'src/geyi_param_ui.c', 'tests/test_param_ui.c']),
    ('geyi-ai-tests', ['src/geyi_game.c', 'src/geyi_ai.c', 'tests/test_ai.c']),
    ('geyi-balance-tests', ['src/geyi_game.c', 'tools/geyi_balance.c', 'tests/test_balance.c']),
    ('geyi-balance-scan', ['src/geyi_game.c', 'tools/geyi_balance.c', 'tools/geyi_balance_scan.c']),
    ('geyi-ai-bench', ['src/geyi_game.c', 'src/geyi_ai.c', 'tools/geyi_ai_bench.c']),
    ('geyi-cli', ['src/geyi_game.c', 'src/geyi_input.c', 'tools/geyi_cli.c']),
]
for name, relative_sources in targets:
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

smoke = run([str(out/'geyi-cli.exe' if os.name=='nt' else out/'geyi-cli')],
    input='w 1\ncancel\nw 1\nok\nresign\nrestart\nquit\n',timeout=10)
assert 'COMMITTED' in smoke and 'Turn=BLUE' in smoke and 'PREVIEW ONLY' in smoke, 'CLI smoke behavior failed'
(out/'host_test.log').write_text('\n'.join(logs), encoding='utf-8')
print('CLI ready:',out/'geyi-cli.exe')
if '--play' in sys.argv:
    subprocess.run([str(out/('geyi-cli.exe' if os.name=='nt' else 'geyi-cli'))],cwd=root)
