from pathlib import Path
from common import decode, ffmpeg_on_path, prepare_work, work_arguments, write_json

parser = work_arguments('Separate a short backing excerpt with CPU Demucs.')
parser.add_argument('--start', type=float, default=32)
parser.add_argument('--duration', type=float, default=16)
parser.add_argument('--model-dir', type=Path)
parser.add_argument('--threads', type=int, default=4)
args = parser.parse_args()
if args.start < 0 or args.duration <= 0 or args.threads < 1:
    parser.error('start must be nonnegative; duration and threads must be positive')
root = prepare_work(args)
output = root / 'instrument-study'
output.mkdir(exist_ok=True)
source = output / 'input.wav'
decode(root / 'stems/instrumental.wav', source, args.start, args.duration)
import torch
from audio_separator.separator import Separator

torch.set_num_threads(args.threads)


class CpuSeparator(Separator):
    def setup_torch_device(self, system_info):
        self.torch_device_cpu = torch.device('cpu')
        self.torch_device = self.torch_device_cpu
        self.torch_device_mps = None
        self.onnx_execution_provider = ['CPUExecutionProvider']


with ffmpeg_on_path():
    separator = CpuSeparator(
        model_file_dir=str(args.model_dir.expanduser().resolve() if args.model_dir else root / 'models'),
        output_dir=str(output), output_format='WAV', use_soundfile=True,
        normalization_threshold=.98,
        demucs_params={'segment_size': 'Default', 'shifts': 1, 'overlap': .25, 'segments_enabled': True},
    )
    separator.load_model('htdemucs.yaml')
    separator.separate(str(source), custom_output_names={
        'Bass': 'bass', 'Drums': 'drums', 'Other': 'melodic', 'Vocals': 'vocal_leakage',
    })
write_json(output / 'excerpt.json', {'source_start_seconds': args.start, 'duration_seconds': args.duration})
