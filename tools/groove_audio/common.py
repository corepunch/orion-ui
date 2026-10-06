from pathlib import Path
from contextlib import contextmanager
import argparse
import json
import os
import shutil
import subprocess
import tempfile


def work_arguments(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument('--work-dir', type=Path, required=True,
                        help='Local workspace containing stems, analyses and exports')
    return parser


def prepare_work(args):
    root = args.work_dir.expanduser().resolve()
    root.mkdir(parents=True, exist_ok=True)
    return root


def ffmpeg():
    executable = shutil.which('ffmpeg')
    if executable:
        return executable
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


@contextmanager
def ffmpeg_on_path():
    executable = ffmpeg()
    previous = os.environ.get('PATH')
    with tempfile.TemporaryDirectory(prefix='groove-ffmpeg-') as directory:
        Path(directory, 'ffmpeg').symlink_to(executable)
        os.environ['PATH'] = directory + os.pathsep + (previous or '')
        try:
            yield
        finally:
            if previous is None:
                os.environ.pop('PATH', None)
            else:
                os.environ['PATH'] = previous


def decode(source, target, start=None, duration=None):
    command = [ffmpeg(), '-hide_banner', '-loglevel', 'error', '-y']
    if start is not None:
        command += ['-ss', str(start)]
    command += ['-i', str(source)]
    if duration is not None:
        command += ['-t', str(duration)]
    command += ['-vn', '-ar', '44100', '-ac', '2', '-c:a', 'pcm_f32le', str(target)]
    subprocess.run(command, check=True)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, default=lambda x: x.item()) + '\n')
