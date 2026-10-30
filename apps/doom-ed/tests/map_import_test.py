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
        assert (result.returncode == 0) == (variant == "valid"), (variant, result.stdout, result.stderr)
        if variant == "valid":
            assert "vertices=4 lines=4 sectors=1" in result.stdout, result.stdout
        print("Map import:", variant, "passed")
