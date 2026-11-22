"""Reproducible codec comparison. Run on native state images, not padded language fixtures."""
import argparse
import bz2
import gc
import json
import lzma
import os
import pathlib
import platform
import statistics
import subprocess
import sys
import time
import zlib
import lz4.frame
import psutil
import zstandard as zstd
import brotli


def codec(name):
    if name.startswith("zstd-"):
        level = int(name.split("-")[1])
        params = zstd.ZstdCompressionParameters.from_level(level, window_log=20, write_checksum=0)
        return zstd.ZstdCompressor(compression_params=params).compress, zstd.ZstdDecompressor().decompress
    if name == "lz4hc-12":
        return lambda b: lz4.frame.compress(b, compression_level=12, content_checksum=False, block_checksum=False), lz4.frame.decompress
    if name.startswith("lzma1-"):
        size = int(name.split("-")[1]) * 1024 * 1024
        filters = [{"id": lzma.FILTER_LZMA1, "preset": 9, "dict_size": size, "lc": 3, "lp": 0, "pb": 2}]
        return lambda b: lzma.compress(b, format=lzma.FORMAT_RAW, filters=filters), lambda b: lzma.decompress(b, format=lzma.FORMAT_RAW, filters=filters)
    if name.startswith("lzma2-"):
        size = int(name.split("-")[1]) * 1024 * 1024
        return lambda b: lzma.compress(b, format=lzma.FORMAT_XZ, check=lzma.CHECK_NONE, filters=[{"id": lzma.FILTER_LZMA2, "preset": 9, "dict_size": size}]), lzma.decompress
    if name == "deflate-9":
        def compress(b):
            c = zlib.compressobj(9, wbits=-15)
            return c.compress(b) + c.flush()
        return compress, lambda b: zlib.decompress(b, wbits=-15)
    if name == "bzip2-9":
        return lambda b: bz2.compress(b, compresslevel=9), bz2.decompress
    if name == "brotli-11":
        return lambda b: brotli.compress(b, quality=11, lgwin=20), brotli.decompress
    raise ValueError(name)


def timed(function, data, seconds=0.15):
    samples = []
    for _ in range(3):
        start = time.perf_counter()
        n = 0
        while time.perf_counter() - start < seconds:
            result = function(data)
            n += 1
        samples.append((time.perf_counter() - start) / n)
    return statistics.median(samples), result


def memory_child(name, file, phase):
    compress, decompress = codec(name)
    operation = compress if phase == "compressor" else decompress
    packed = pathlib.Path(file).read_bytes()
    gc.collect()
    process = psutil.Process()
    baseline = process.memory_info().rss
    def os_peak():
        info = process.memory_info()
        if hasattr(info, "peak_wset"):
            return info.peak_wset
        import resource
        return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * 1024
    baseline_peak = os_peak()
    peak = baseline
    stop = time.perf_counter() + 0.4
    while time.perf_counter() < stop:
        result = operation(packed)
        peak = max(peak, process.memory_info().rss)
        result = None
    print(json.dumps({phase + "_peak_increment_kib": max(0, os_peak() - baseline_peak) / 1024,
        phase + "_resident_increment_kib": (peak - baseline) / 1024}))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", nargs="+", type=pathlib.Path)
    p.add_argument("--output", type=pathlib.Path)
    p.add_argument("--memory-child", nargs=3)
    p.add_argument("--codec", nargs="+", help="Measure only the selected codecs")
    args = p.parse_args()
    if args.memory_child:
        return memory_child(*args.memory_child)
    names = args.codec or ["zstd-9", "zstd-15", "zstd-19", "lz4hc-12", "lzma1-1", "lzma2-1", "lzma2-8", "deflate-9", "bzip2-9", "brotli-11"]
    records = []
    temporary = args.output.parent / "codec-memory-input.bin"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for file in args.input:
        raw = file.read_bytes()
        for name in names:
            compress, decompress = codec(name)
            ct, packed = timed(compress, raw)
            dt, decoded = timed(decompress, packed)
            assert decoded == raw
            temporary.write_bytes(packed)
            memory = subprocess.check_output([sys.executable, __file__, "--memory-child", name, str(temporary), "decoder"], text=True)
            compressor_memory = subprocess.check_output([sys.executable, __file__, "--memory-child", name, str(file), "compressor"], text=True)
            record = {"file": file.name, "codec": name, "raw_bytes": len(raw), "packed_bytes": len(packed),
                "compress_ms": ct * 1000, "decompress_ms": dt * 1000,
                "decompress_mib_s": len(raw) / dt / 1024**2, **json.loads(memory), **json.loads(compressor_memory)}
            records.append(record)
            print(json.dumps(record), flush=True)
    temporary.unlink()
    result = {"platform": platform.platform(), "processor": platform.processor(), "python": sys.version,
        "zstandard": zstd.__version__, "zstd_native": zstd.ZSTD_VERSION,
        "brotli": brotli.__version__,
        "zstd_decoder_context_bytes": zstd.ZstdDecompressor().memory_size(),
        "method": "3 median timing batches; OS peak working-set increments in isolated Python children, plus post-call resident increments; includes output buffer, excludes interpreter/input baseline; zstd window_log=20",
        "records": records}
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
