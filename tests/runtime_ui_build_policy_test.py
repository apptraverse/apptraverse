"""Verify the generated Ninja consumer compile graph; non-Debug builds are not run."""
from pathlib import Path
import sys
build = Path(sys.argv[1])
ninja = (build / 'build.ninja').read_text().replace('\\', '/')
for target in ('win32_main_window_runtime_demo', 'win32_dynamic_objects_demo'):
    objects = [line for line in ninja.splitlines() if line.startswith('build ') and f'{target}_load_only.dir/' in line and '.obj:' in line]
    assert objects, target
    assert not any('runtime_ui_supply.cpp' in line for line in objects), target
    assert any('runtime_ui_model.cpp' in line for line in objects), target
print('PASS generated load-only compile graph: model present, initial graph producer absent; non-Debug builds NOT RUN')
