"""Exercise the built application's real map importer without a display."""
import pathlib
import struct
import subprocess
import sys
import tempfile


def fixture(path, variant):
    vertices = b"".join(struct.pack("<hh", *p) for p in [(0, 0), (128, 0), (128, 128), (0, 128)])
    lines = b"".join(struct.pack("<HHH6BHH", i, (i + 1) % 4 if variant != "vertex" else 99,
                                 0, 0, 0, 0, 0, 0, 0, i, 65535) for i in range(4))
    sides = b"".join(struct.pack("<hh8s8s8sH", 0, 0, b"-", b"-", b"STONE", 0 if variant != "sector" else 99) for _ in range(4))
    sector = struct.pack("<hh8s8shhh", 0, 128, b"FLOOR", b"CEILING", 160, 0, 0)
    lumps = [("MAP01", b""), ("THINGS", b""), ("LINEDEFS", lines), ("SIDEDEFS", sides),
             ("VERTEXES", vertices if variant != "record" else vertices + b"x"),
             ("SEGS", b""), ("SSECTORS", b""), ("NODES", b""), ("SECTORS", sector),
             ("REJECT", b""), ("BLOCKMAP", b"")]
    data = bytearray(b"PWAD" + b"\0" * 8)
    entries = []
    for name, payload in lumps:
        entries.append(struct.pack("<II8s", len(data), len(payload), name.encode()))
        data.extend(payload)
    data[4:12] = struct.pack("<II", len(entries), len(data))
    path.write_bytes(data + b"".join(entries))


with tempfile.TemporaryDirectory(prefix="orion-doom-import-") as temp:
    for variant in ["valid", "vertex", "sector", "record"]:
        path = pathlib.Path(temp) / (variant + ".wad")
        fixture(path, variant)
        result = subprocess.run([sys.argv[1], "--check-wad", str(path)], capture_output=True, text=True, timeout=30)
        assert (result.returncode == 0) == (variant == "valid"), (variant, result.returncode, result.stdout, result.stderr)
        if variant == "valid":
            assert "vertices=4 lines=4 sectors=1" in result.stdout, result.stdout
        print("Map import:", variant, "passed")

# Optional display-backed smoke check; the fixture contains only generated data.
# Run under xvfb-run on Linux. Keep captures outside the checkout for CI upload.
if len(sys.argv) == 3:
    captures = pathlib.Path(sys.argv[2]).resolve()
    captures.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="orion-doom-render-") as temp:
        path = pathlib.Path(temp) / "render.wad"
        fixture(path, "valid")
        original = path.read_bytes()
        count, directory = struct.unpack_from("<II", original, 4)
        lumps = []
        for i in range(count):
            offset, size, name = struct.unpack_from("<II8s", original, directory + i * 16)
            payload = original[offset:offset + size]
            if name.rstrip(b"\0") == b"THINGS":
                payload = struct.pack("<7h6B", 0, 64, 64, 0, 0, 1, 7, 0, 0, 0, 0, 0, 0)
            lumps.append((name, payload))
        lumps += [(b"PLAYPAL", bytes(c for i in range(256) for c in (i, i, i))),
                  (b"TEXTURE1", struct.pack("<I", 0)), (b"PNAMES", struct.pack("<I", 0)),
                  (b"F_START", b""), (b"FLOOR", bytes([100]) * 4096),
                  (b"CEILING", bytes([180]) * 4096), (b"F_END", b"")]
        data, entries = bytearray(b"PWAD" + b"\0" * 8), []
        for name, payload in lumps:
            entries.append(struct.pack("<II8s", len(data), len(payload), name))
            data.extend(payload)
        data[4:12] = struct.pack("<II", len(entries), len(data))
        path.write_bytes(data + b"".join(entries))
        for view, options in [("editor", []), ("preview", ["--3d"])]:
            capture = captures / (view + ".jpg")
            subprocess.run([sys.argv[1], str(path), "--offscreen", *options,
                            "--screenshot", str(capture)], check=True, timeout=30)
            assert capture.stat().st_size > 1000, capture
            assert capture.read_bytes().startswith(b"\xff\xd8"), capture
            print("Rendered:", view)
    assert (captures / "editor.jpg").read_bytes() != (captures / "preview.jpg").read_bytes(), "Both views rendered the same empty shell"
