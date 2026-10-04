#!/usr/bin/env python3
"""Install the pinned CPU Fun-ASR weights, with SHA256 verification."""
import argparse
import hashlib
from pathlib import Path
import shutil
import urllib.request

FILES = [
    ("FunAudioLLM/Fun-ASR-Nano-GGUF", "46e849502a867080d66d351b8dfb1018b607e509",
     "funasr-encoder-f16.gguf", "f92f91d01a24fbed6c863495b2ee8c6a6788144a02858b75743f0946668de8a2", "fun-nano"),
    ("FunAudioLLM/Fun-ASR-Nano-GGUF", "46e849502a867080d66d351b8dfb1018b607e509",
     "qwen3-0.6b-q8_0.gguf", "819f385dc0e035dccc3d9e7edaf6b7b044b8ba7ace63cbcbf84c7e397eecbf27", "fun-nano"),
    ("FunAudioLLM/fsmn-vad-GGUF", "6840bae4c5c92ee8c04faaf4db23dd0105098d7f",
     "fsmn-vad.gguf", "1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479", "fun-vad"),
]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=Path.home() / ".local/share/hyprvoice/models/fun-asr-nano")
    parser.add_argument("--cache", type=Path, help="Reuse verified files from the previous public benchmark")
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)
    for repo, revision, name, sha, folder in FILES:
        target = args.destination / name
        if target.is_file() and digest(target) == sha:
            print("Verified", name, flush=True)
            continue
        temporary = target.with_suffix(".gguf.download")
        try:
            cached = args.cache / folder / name if args.cache else None
            if cached and cached.is_file() and digest(cached) == sha:
                shutil.copyfile(cached, temporary)
            else:
                print("Downloading", name, flush=True)
                url = f"https://huggingface.co/{repo}/resolve/{revision}/{name}"
                with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as stream:
                    shutil.copyfileobj(response, stream, 1024 * 1024)
            if digest(temporary) != sha:
                raise ValueError(f"SHA256 mismatch: {name}")
            temporary.replace(target)
            print("Installed verified", name, flush=True)
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
