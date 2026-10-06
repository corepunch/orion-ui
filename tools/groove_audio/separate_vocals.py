from pathlib import Path
import subprocess
import sys
from common import decode, ffmpeg_on_path, prepare_work, work_arguments

parser = work_arguments('Separate full-length vocals and backing with Mel-Band RoFormer.')
parser.add_argument('input', type=Path)
parser.add_argument('--model-dir', type=Path)
parser.add_argument('--segment-size', type=int, default=512)
parser.add_argument('--overlap', type=int, default=4)
parser.add_argument('--full-precision', action='store_true')
args = parser.parse_args()
if args.segment_size <= 0 or args.overlap < 1:
    parser.error('segment size and overlap must be positive')
root = prepare_work(args)
source = args.input.expanduser().resolve()
if not source.is_file():
    parser.error(f'input does not exist: {source}')
stems = root / 'stems'
stems.mkdir(exist_ok=True)
decode(source, root / 'input.wav')
model_dir = args.model_dir.expanduser().resolve() if args.model_dir else root / 'models'
command = [str(Path(sys.executable).with_name('audio-separator')), str(root / 'input.wav'),
           '-m', 'vocals_mel_band_roformer.ckpt', '--model_file_dir', str(model_dir),
           '--output_dir', str(stems), '--output_format', 'WAV', '--use_soundfile',
           '--normalization', '.98', '--mdxc_overlap', str(args.overlap),
           '--mdxc_segment_size', str(args.segment_size), '--mdxc_override_model_segment_size',
           '--custom_output_names', '{"Vocals":"vocals_full","Instrumental":"instrumental","Other":"instrumental"}']
if not args.full_precision:
    command += ['--use_native_fp16']
# The separator launches ffmpeg by name even when imageio supplies the executable.
with ffmpeg_on_path():
    subprocess.run(command, check=True)
