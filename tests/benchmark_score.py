#!/usr/bin/env python3
"""Complete-manifest CER, English WER, mixed-token MER, and runtime summaries."""
import argparse
from collections import defaultdict
import hashlib
import json
import re
import unicodedata
from pathlib import Path

from score import distance, normalized


def tokens(text):
    text = unicodedata.normalize("NFKC", text).casefold()
    # One Han character or one alphanumeric English token. Preserve contractions.
    return mixed_tokens(text.replace("’", "'"))


def mixed_tokens(text):
    output, latin = [], ""
    for c in text:
        if "\u3400" <= c <= "\u9fff":
            if latin:
                output.extend(re.findall(r"[^\W_]+(?:'[^\W_]+)*", latin))
                latin = ""
            output.append(c)
        else:
            latin += c
    output.extend(re.findall(r"[^\W_]+(?:'[^\W_]+)*", latin))
    return output


def read(path):
    rows = {}
    for line in path.read_text().splitlines():
        row = json.loads(line)
        if row["id"] in rows:
            raise ValueError("Duplicate ID: " + row["id"])
        rows[row["id"]] = row
    return rows


def percentile(values, q):
    values = sorted(values)
    if not values:
        return None
    point = (len(values) - 1) * q
    lo, hi = int(point), min(int(point) + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (point - lo)


def evaluate(cases, rows):
    if cases.keys() != rows.keys():
        raise ValueError("Missing/extra result IDs; incomplete runs cannot be scored")
    groups = defaultdict(lambda: {"cases": 0, "characters": 0, "character_edits": 0,
                                  "tokens": 0, "token_edits": 0, "errors": 0,
                                  "rewrite_errors": 0, "auto_commit_blocked": 0, "false_insertions": 0,
                                  "audio_seconds": 0, "compute_ms": 0, "timed_cases": 0, "exact_cases": 0,
                                  "asr_times": [], "rewrite_times": []})
    details = []
    for key, case in cases.items():
        row = rows[key]
        ref, hyp = normalized(case["reference"]), normalized(row.get("text", ""))
        rt, ht = tokens(case["reference"]), tokens(row.get("text", ""))
        ce, te = distance(ref, hyp), distance(rt, ht)
        group = groups[case["category"]]
        group["cases"] += 1
        group["characters"] += len(ref)
        group["character_edits"] += ce
        group["tokens"] += len(rt)
        group["token_edits"] += te
        group["errors"] += int("error" in row)
        group["rewrite_errors"] += int("rewrite_error" in row)
        group["auto_commit_blocked"] += int(row.get("auto_commit_blocked", False))
        group["false_insertions"] += int(not case["reference"].strip() and bool(row.get("text", "").strip()))
        group["exact_cases"] += int(ref == hyp)
        seconds = case.get("duration_seconds", case.get("duration", 0))
        group["audio_seconds"] += seconds
        ms = row.get("asr_ms", row.get("total_ms", row.get("wall_ms")))
        rewrite = row.get("rewrite_ms", 0)
        if ms is not None:
            group["compute_ms"] += ms
            group["timed_cases"] += 1
            group["asr_times"].append(ms)
        if row.get("llm_called"):
            group["rewrite_times"].append(rewrite)
        details.append({"id": key, "category": case["category"], "character_edits": ce,
                        "characters": len(ref), "token_edits": te, "tokens": len(rt),
                        "base_id": case.get("base_id", key)})
    for group in groups.values():
        group["cer"] = group["character_edits"] / group["characters"] if group["characters"] else None
        group["mer"] = group["token_edits"] / group["tokens"] if group["tokens"] else None
        if group["timed_cases"] != group["cases"]:
            group["compute_ms"] = None
        group["compute_rtf"] = group["compute_ms"] / 1000 / group["audio_seconds"] if group["compute_ms"] is not None and group["audio_seconds"] else None
        times, rewrites = group.pop("asr_times"), group.pop("rewrite_times")
        group["asr_p50_ms"], group["asr_p95_ms"] = percentile(times, .5), percentile(times, .95)
        group["rewrite_calls"] = len(rewrites)
        group["rewrite_p50_ms"], group["rewrite_p95_ms"] = percentile(rewrites, .5), percentile(rewrites, .95)
    return {"normalization": "NFKC + casefold; CER keeps alphanumeric; MER uses Han chars + English words; no number verbalization equivalence",
            "timing": "compute_ms/compute_rtf and asr percentiles describe ASR only; rewrite times are separate; unknown timing is null",
            "summary": dict(groups), "per_case": details}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", type=Path)
    p.add_argument("results", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--verify-audio", action="store_true")
    args = p.parse_args()
    cases = read(args.manifest)
    if args.verify_audio:
        for case in cases.values():
            audio = Path(case["audio"])
            if not audio.is_absolute():
                audio = args.manifest.parent / audio
            if hashlib.sha256(audio.read_bytes()).hexdigest() != case["sha256"]:
                raise ValueError("Audio hash mismatch: " + case["id"])
    args.output.write_text(json.dumps(evaluate(cases, read(args.results)), ensure_ascii=False, indent=2) + "\n")
