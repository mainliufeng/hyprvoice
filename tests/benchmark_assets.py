#!/usr/bin/env python3
"""Download pinned public benchmark assets, verifying published file hashes."""
import argparse
import hashlib
import json
import os
import time
import urllib.error
import urllib.request
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def git_blob(path):
    data = path.read_bytes()
    return hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()


def fetch_json(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=15) as response:
                return json.load(response)
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--only", nargs="+", choices=["ascend", "qwen06", "qwen17", "fun-nano", "fun-vad"])
    args = p.parse_args()
    args.output = args.output.resolve()
    # Keep SDK caches in the isolated benchmark directory; never use implicit tokens.
    os.environ["HF_HOME"] = str(args.output / "hf-cache")
    os.environ["HF_HUB_DISABLE_IMPLICIT_TOKEN"] = "1"
    from huggingface_hub import hf_hub_download

    tasks = [
        ("CAiRE/ASCEND", "datasets", "ascend", "737e9800ae31be9932ba8464c80366559bd28424",
         ["main/test-00000-of-00001.parquet", "README.md"]),
        ("FunAudioLLM/fsmn-vad-GGUF", "models", "fun-vad", "6840bae4c5c92ee8c04faaf4db23dd0105098d7f",
         ["fsmn-vad.gguf"]),
        ("FunAudioLLM/Fun-ASR-Nano-GGUF", "models", "fun-nano", "46e849502a867080d66d351b8dfb1018b607e509",
         ["funasr-encoder-f16.gguf", "qwen3-0.6b-q8_0.gguf"]),
        ("Qwen/Qwen3-ASR-0.6B", "models", "qwen06", "5eb144179a02acc5e5ba31e748d22b0cf3e303b0",
         ["config.json", "generation_config.json", "model.safetensors", "vocab.json", "merges.txt"]),
        ("Qwen/Qwen3-ASR-1.7B", "models", "qwen17", "7278e1e70fe206f11671096ffdd38061171dd6e5",
         ["config.json", "generation_config.json", "model.safetensors.index.json",
          "model-00001-of-00002.safetensors", "model-00002-of-00002.safetensors", "vocab.json", "merges.txt"]),
    ]
    args.output.mkdir(parents=True, exist_ok=True)
    jobs, sources = [], []
    for repo, kind, folder, revision, files in tasks:
        if args.only and folder not in args.only:
            continue
        # Query the exact revision, not moving main.
        data = fetch_json(f"https://huggingface.co/api/{kind}/{repo}/revision/{revision}?blobs=true")
        assert data["sha"] == revision
        siblings = {x["rfilename"]: x for x in data["siblings"]}
        source = {"repo": repo, "revision": revision, "folder": folder, "files": []}
        for filename in files:
            item = siblings[filename]
            sha = item.get("lfs", {}).get("sha256")
            target = args.output / folder / filename
            blob = item["blobId"] if not sha else None
            jobs.append((repo, kind, revision, filename, target, item.get("size"), sha, blob))
            source["files"].append({"name": filename, "size": item.get("size"), "sha256": sha,
                                    "git_blob_sha1": blob})
        sources.append(source)
    (args.output / "sources.json").write_text(json.dumps(sources, indent=2) + "\n")
    for repo, kind, revision, filename, target, size, sha, blob in jobs:
        def valid_file():
            return target.exists() and target.stat().st_size == size and (digest(target) == sha if sha else git_blob(target) == blob)
        valid = valid_file()
        if not valid:
            print(f"Downloading: {repo}/{filename}", flush=True)
            hf_hub_download(repo, filename, revision=revision, repo_type="dataset" if kind == "datasets" else "model",
                            local_dir=target.parents[len(Path(filename).parts) - 1], token=False,
                            force_download=target.exists())
        if not valid_file():
            raise ValueError(f"Size/hash mismatch: {target}")
        print(f"Verified: {repo}/{filename}", flush=True)


if __name__ == "__main__":
    main()
