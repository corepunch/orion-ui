from pathlib import Path
import os
import re
import shlex
import subprocess
from common import prepare_work, work_arguments, write_json

parser = work_arguments('Build the offline renderer from the current Groove C synth.')
parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
args = parser.parse_args()
root = prepare_work(args)
repository = Path(__file__).resolve().parents[2]
build = root / 'build'
build.mkdir(exist_ok=True)
header = (repository / 'apps/groove/synth.h').read_text()
groove = (repository / 'apps/groove/groove.h').read_text()
constants = '\n'.join(re.search(r'^#define\s+' + name + r'\s+\d+', groove, re.M).group()
                      for name in ['GR_SAMPLE_RATE', 'GR_BEATS_BAR'])
standalone = '''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
'''
header = header.replace('#include "groove.h"', standalone + constants)
(build / 'standalone_synth.h').write_text(header)
# Read the enum from the engine, rather than maintaining another instrument table.
plain = re.sub(r'//[^\n]*|/\*.*?\*/', '', header, flags=re.S)
enum = re.search(r'typedef enum\s*{([^{}]*)}\s*sy_inst_t;', plain).group(1)
names = [entry.strip() for entry in enum.split(',') if entry.strip()]
if any(not re.fullmatch(r'I_[A-Z0-9_]+', name) for name in names):
    raise ValueError('Instrument enum has explicit values; update the renderer index parser')
write_json(build / 'instrument-index.json', dict(zip(names, range(len(names)))))
command = shlex.split(args.cc) + ['-std=c99', '-O2', '-Wall', '-Wextra',
          '-I', str(build), '-include', str(build / 'standalone_synth.h'),
          str(repository / 'apps/groove/synth.c'), str(Path(__file__).with_name('render_study.c')),
          '-lm', '-o', str(build / 'render_study')]
subprocess.run(command, check=True)
print(build / 'render_study')
