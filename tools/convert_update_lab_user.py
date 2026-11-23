"""Convert a portable USER export from the v2 Zstd store to the LZMA1 format."""
import argparse
import struct
import subprocess
import tempfile
from pathlib import Path

import zstandard


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--packer", type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.destination.resolve():
        parser.error("choose a separate destination to preserve the original export")
    limit = 256 * 1024 * 1024
    header = struct.Struct("<IIIQI")
    with args.source.open("rb") as source:
        fields = source.read(header.size)
        if len(fields) != header.size:
            parser.error("incomplete legacy header")
        kind, start, release, size, packed_size = header.unpack(fields)
        if kind != 0 or start != release or not 0 < size <= limit or not 0 < packed_size <= limit:
            parser.error("expected a legacy full USER export")
        payload = source.read(packed_size)
        if len(payload) != packed_size or source.read(1):
            parser.error("unexpected legacy file length")
    decoder = zstandard.ZstdDecompressor(format=zstandard.FORMAT_ZSTD1_MAGICLESS)
    with decoder.stream_reader(payload) as stream:
        native = stream.read(size + 1)
        if len(native) != size or stream.read(1):
            parser.error("unexpected native USER length")
    with tempfile.TemporaryDirectory() as directory:
        raw = Path(directory) / "user.bin"
        raw.write_bytes(native)
        subprocess.run([args.packer.resolve(), "full", raw, str(release), args.destination], check=True)


if __name__ == "__main__":
    main()
