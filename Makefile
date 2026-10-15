# 便捷入口：所有命令都只是调用 scripts/ 下的 Python 脚本，没有 make 的同学直接运行脚本即可。
PY ?= python3

.PHONY: setup env build check final devlog clean

setup:
	$(PY) scripts/setup_course.py

env:
	$(PY) scripts/check_env.py

build:
	$(PY) scripts/build_firmware.py

check:
	$(PY) scripts/check_all.py

final:
	$(PY) scripts/check_all.py --final

devlog:
	$(PY) scripts/new_devlog.py

clean:
	rm -rf build
