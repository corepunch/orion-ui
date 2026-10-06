from common import prepare_work, work_arguments, write_json

parser = work_arguments('Optional approximate English word alignment with MLX Whisper.')
parser.add_argument('--model', default='mlx-community/whisper-small.en-mlx',
                    help='Hugging Face model ID or local MLX model directory')
parser.add_argument('--prompt', default='', help='Optional short lyric hints')
args = parser.parse_args()
root = prepare_work(args)
import mlx.core as mx
import mlx_whisper

mx.set_default_device(mx.gpu)
result = mlx_whisper.transcribe(
    str(root / 'stems/vocals_full.wav'), path_or_hf_repo=args.model,
    language='en', word_timestamps=True, verbose=False,
    condition_on_previous_text=False, temperature=0,
    initial_prompt=args.prompt or None, hallucination_silence_threshold=2,
)
write_json(root / 'lyrics-alignment.json', result)
for segment in result['segments']:
    print(f"{segment['start']:7.2f} - {segment['end']:7.2f}  {segment['text']}")
