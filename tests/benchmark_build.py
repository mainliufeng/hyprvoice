#!/usr/bin/env python3
"""Build pinned external CPU runtimes and the resident benchmark adapters."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request

SOURCES = [
    ("qwen-asr", "antirez/qwen-asr", "924694251d9e0f18e5d86bbd06aa3ab5f870002d"),
    ("fun-asr", "QwenAudio/Fun-ASR", "0339018ba74a7defa3b6b6a96718d17b816be77b"),
    ("llama.cpp", "ggml-org/llama.cpp", "8086439a4cea94c71a5dfb8fe4ad1546aebd640f"),
]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--openblas-prefix", type=Path, required=True,
                   help="Prefix containing include/openblas/cblas.h and lib/libopenblas.so")
    args = p.parse_args()
    base, blas = args.output.resolve(), args.openblas_prefix.resolve()
    app = Path(__file__).resolve().parents[1]
    include = blas / "include/openblas"
    if not (include / "cblas.h").exists() or not (blas / "lib/libopenblas.so").exists():
        p.error("Missing OpenBLAS headers/library; do not benchmark an unaccelerated fallback")
    base.mkdir(parents=True, exist_ok=True)
    source_records = []
    for folder, repo, revision in SOURCES:
        target, archive = base / folder, base / (folder + ".tar.gz")
        url = f"https://codeload.github.com/{repo}/tar.gz/{revision}"
        if not archive.exists():
            with urllib.request.urlopen(url, timeout=180) as response, archive.open("wb") as out:
                while data := response.read(1024 * 1024):
                    out.write(data)
        if not target.exists():
            with tarfile.open(archive) as tar:
                members = tar.getmembers()
                root = members[0].name.split('/')[0]
                tar.extractall(base, filter="data")
            (base / root).rename(target)
        # Refuse modified/stale runtime sources: a pinned URL alone does not
        # prove that an existing extracted tree still matches that revision.
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                if not member.isfile():
                    continue
                relative = Path(member.name).parts[1:]
                path = target.joinpath(*relative)
                with tar.extractfile(member) as expected:
                    if not path.is_file() or path.read_bytes() != expected.read():
                        raise ValueError(f"External source differs from pinned archive: {path}")
        source_records.append({"repo": repo, "revision": revision, "archive_url": url,
                               "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()})

    def run(cmd, cwd=None):
        subprocess.run([str(x) for x in cmd], cwd=cwd, check=True)

    qwen = base / "qwen-asr"
    cflags = f"-Wall -Wextra -O3 -march=native -ffast-math -DUSE_BLAS -DUSE_OPENBLAS -I{include}"
    ldflags = f"-lm -lpthread -L{blas / 'lib'} -Wl,-rpath,{blas / 'lib'} -lopenblas"
    run(["make", "clean-cpu"], qwen)
    run(["make", "qwen_asr", "-j4", "CFLAGS=" + cflags, "LDFLAGS=" + ldflags], qwen)
    run(["g++", "-O3", "-std=c++20", "-I" + str(app / "third_party"), "-I" + str(qwen),
         app / "tests/qwen_benchmark.cpp", *sorted(x for x in qwen.glob("*.o") if x.name != "main.o"),
         "-lm", "-lpthread", "-L" + str(blas / "lib"), "-Wl,-rpath," + str(blas / "lib"),
         "-lopenblas", "-o", base / "qwen_benchmark"])
    fun, llama = base / "fun-asr/runtime/llama.cpp", base / "llama.cpp"
    build = base / "fun-build"
    run(["cmake", "-S", fun, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
         "-DFETCHCONTENT_SOURCE_DIR_LLAMA=" + str(llama), "-DGGML_NATIVE=ON",
         "-DGGML_CUDA=OFF", "-DGGML_VULKAN=OFF"])
    run(["cmake", "--build", build, "--target", "llama-funasr-cli", "-j4"])
    ggml = build / "_deps/llama-build/ggml/src"
    run(["g++", "-O3", "-std=c++20", "-I" + str(app / "third_party"),
         "-I" + str(fun / "funasr-cli"), "-I" + str(fun / "funasr-common"),
         "-I" + str(llama / "include"), "-I" + str(llama / "ggml/include"),
         app / "tests/fun_benchmark.cpp", build / "_deps/llama-build/src/libllama.a",
         ggml / "libggml.a", ggml / "libggml-cpu.a", ggml / "libggml-base.a",
         "-fopenmp", "-lpthread", "-ldl", "-lm", "-o", base / "fun_benchmark"])
    record = {"sources": source_records, "qwen_cflags": cflags, "qwen_ldflags": ldflags,
              "candidate_threads": 8, "qwen_segment_seconds": 20, "fun_vad_max_segment_ms": 30000}
    (base / "runtime-build.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
