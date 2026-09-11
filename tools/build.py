"""Build the firmware with pinned dependencies and verified RAM optimization."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)

if __name__ == '__main__':
    run(sys.executable, '-m', 'platformio', 'pkg', 'install')
    run(sys.executable, str(ROOT/'tools/patch_gfx_progmem.py'), str(ROOT/'.pio/libdeps/esp12e/GFX Library for Arduino'))
    run(sys.executable, str(ROOT/'tools/generate_fonts.py'), str(ROOT/'src/scene/SceneFonts.cpp'))
    run(sys.executable, '-m', 'platformio', 'run', '-t', 'clean')
    run(sys.executable, '-m', 'platformio', 'run')
    print('Build complete:', ROOT/'.pio/build/esp12e/firmware.bin')
    print('No firmware or filesystem was uploaded.')
