#!/usr/bin/env python3
"""Run the production text stage on explicitly supplied benchmark transcripts."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

from benchmark_score import read


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("config", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("asr_results", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--probe", type=Path, required=True)
    p.add_argument("--env-file", type=Path)
    p.add_argument("--workers", type=int, default=1)
    p.add_argument("--mode", choices=["context", "correct"], default="context")
    args = p.parse_args()
    if not 1 <= args.workers <= 4:
        p.error("workers must be between 1 and 4")
    cases, results = read(args.manifest), read(args.asr_results)
    if not cases.keys() <= results.keys():
        p.error("Missing ASR results for benchmark cases")
    if any("error" in results[key] for key in cases):
        p.error("ASR failures must be resolved before running the text stage")
    env = os.environ.copy()
    config = json.loads(args.config.read_text())
    key_name = config["llm"]["api_key_env"]
    if args.env_file:
        # Parse literal systemd-style KEY=value, without executing shell code or
        # importing unrelated environment entries. Never log the key.
        for line in args.env_file.read_text().splitlines():
            for field in shlex.split(line, comments=True):
                if field.startswith(key_name + "="):
                    env[key_name] = field.split("=", 1)[1]
    if not env.get(key_name):
        p.error("Configured API key environment variable is missing")
    items = [{**results[key], "before": cases[key].get("before", "")} for key in cases]
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def run(index):
        stem = args.output.with_suffix(f".part{index}")
        input_path = stem.with_suffix(stem.suffix + ".input.jsonl")
        output_path = stem.with_suffix(stem.suffix + ".jsonl")
        input_path.write_text("".join(json.dumps(x, ensure_ascii=False) + "\n" for x in items[index::args.workers]))
        with output_path.open("w") as out, stem.with_suffix(stem.suffix + ".stderr").open("w") as err:
            proc = subprocess.run([str(args.probe.resolve()), str(args.config.resolve()),
                                   str(input_path.resolve()), args.mode], stdout=out, stderr=err, env=env)
        return {"part": index, "returncode": proc.returncode}, read(output_path)

    started = time.monotonic()
    combined, statuses = {}, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for status, rows in pool.map(run, range(args.workers)):
            statuses.append(status)
            if combined.keys() & rows.keys():
                raise ValueError("Duplicate output across workers")
            combined.update(rows)
    if cases.keys() != combined.keys():
        raise ValueError("Incomplete workflow run")
    args.output.write_text("".join(json.dumps(combined[key], ensure_ascii=False) + "\n" for key in cases))
    meta = {"parts": statuses, "wall_seconds": time.monotonic() - started,
            "concurrent_requests": args.workers, "count": len(combined),
            "model": config["llm"]["model"], "base_url": config["llm"]["base_url"],
            "mode": args.mode, "failure_retries": 0}
    args.output.with_suffix(".runtime.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(json.dumps(meta))
    raise SystemExit(int(any(x["returncode"] for x in statuses)))


if __name__ == "__main__":
    main()
