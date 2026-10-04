#!/usr/bin/env python3
"""Real desktop/audio acceptance. Run only inside an isolated Hyprland session."""
import argparse
import json
import os
import shlex
import subprocess
import time
import wave
from pathlib import Path
from score import distance, normalized

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("config", type=Path)
parser.add_argument("speech", type=Path)
parser.add_argument("noise", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--llm-env", type=Path)
parser.add_argument("--fcitx", action="store_true")
parser.add_argument("--visual-only", action="store_true")
parser.add_argument("--context-only", action="store_true")
parser.add_argument("--coexist-only", action="store_true")
parser.add_argument("--backend-switch", action="store_true", help="Exercise real Fun selection, cancellation, persistence and X-ASR rollback")
parser.add_argument("--binary", type=Path, help="Exercise a particular installed or baseline binary")
parser.add_argument("--continuity-only", action="store_true", help="Verify quiet prefix and suffix survive real capture and paste")
parser.add_argument("--hardware-source", help="Explicit real microphone; requires --hardware-sink and --continuity-only")
parser.add_argument("--hardware-sink", help="Explicit speaker for an authorized acoustic playback test")
parser.add_argument("--hardware-volume", type=int, default=65536, help="This playback stream's volume only; does not change device volume")
parser.add_argument("--vad-reject-only", action="store_true", help="Use an explicitly generated test-only VAD rejection config")
args = parser.parse_args()
hardware = bool(args.hardware_source or args.hardware_sink)
if hardware and not (args.hardware_source and args.hardware_sink and args.continuity_only):
    parser.error("hardware playback requires both devices and --continuity-only")
if not 1 <= args.hardware_volume <= 131072:
    parser.error("hardware volume must be 1..131072")
if args.vad_reject_only and not args.continuity_only:
    parser.error("VAD rejection test requires --continuity-only")
if args.coexist_only and not args.fcitx:
    parser.error("--coexist-only requires --fcitx")
root = Path(__file__).resolve().parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, GTK_IM_MODULE="none", GDK_BACKEND="wayland")
env["GTK_A11Y"] = "atspi" if args.context_only else "none"
if env.get("XDG_RUNTIME_DIR") == f"/run/user/{os.getuid()}":
    parser.error("Refusing to operate the daily desktop; use an isolated Hyprland runtime")
env["PIPEWIRE_REMOTE"] = f"/run/user/{os.getuid()}/pipewire-0"
env["PULSE_SERVER"] = f"unix:/run/user/{os.getuid()}/pulse/native"
if args.llm_env:
    for line in args.llm_env.read_text().splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            env[key.strip()] = " ".join(shlex.split(value))
config = json.loads(args.config.read_text())
if args.backend_switch:
    config["asr"] = {"backend": "x-asr"}
node = f"hyprvoice_qa_{os.getpid()}"
config["audio_source"] = args.hardware_source if hardware else node + "_source"
config["auto_commit"] = hardware or args.vad_reject_only or (args.context_only and not args.llm_env)
config["context"] = {"enabled": args.context_only, "max_chars": 1024}
config["terminal_classes"].append("hyprvoice-qa-terminal")
if args.llm_env and env.get("DEEPSEEK_BASE_URL"):
    config["llm"]["base_url"] = env["DEEPSEEK_BASE_URL"]
if not args.llm_env:
    env.pop(config["llm"]["api_key_env"], None)
    config["llm"]["model"] = ""
config_path = out / "config.json"
config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2))
env["HYPRVOICE_CONFIG"] = str(config_path)
env["XDG_CONFIG_HOME"] = str(out / "user-config")
env["XDG_DATA_HOME"] = str(out / "user-data")
modules = []
children = []
checks = []
binary = args.binary.resolve() if args.binary else root / "build/hyprvoice"
keyboard = None


def key(command):
    keyboard.stdin.write(command + "\n")
    keyboard.stdin.flush()
    if keyboard.stdout.readline().strip() != "OK":
        raise RuntimeError("Test keyboard request failed")


def run(*command, **kwargs):
    return subprocess.run(command, env=env, check=True, capture_output=True, text=True, timeout=40, **kwargs).stdout


def spawn(*command, **extra):
    log = (out / f"child-{len(children)}.log").open("w")
    process = subprocess.Popen(command, env=dict(env, **extra), stdout=log, stderr=log)
    children.append(process)
    return process


def call(command):
    reply = subprocess.run([str(binary), command], env=env, capture_output=True, text=True, timeout=40)
    if reply.returncode:
        if command != "status":
            print(reply.stdout + reply.stderr, flush=True)
        raise subprocess.CalledProcessError(reply.returncode, command, output=reply.stdout, stderr=reply.stderr)
    return json.loads(reply.stdout)


def wait(fn, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = fn()
        if value:
            return value
        time.sleep(0.1)
    raise RuntimeError("Timed out waiting for acceptance condition")


def phase(name):
    def probe():
        try:
            state = call("status")["state"]
            return state if state["phase"] == name else None
        except subprocess.CalledProcessError:
            return None
    return wait(probe, 40)


def editor(initial="", backend="wayland"):
    path = out / f"editor-{len(children)}.txt"
    path.write_text(initial)
    process = spawn(str(root / "build/test_editor"), str(path), initial, GDK_BACKEND=backend,
                    GTK_IM_MODULE="fcitx" if args.fcitx else "none")
    wait(lambda: any(c["pid"] == process.pid for c in json.loads(run("hyprctl", "clients", "-j"))))
    run("hyprctl", "dispatch", "focuswindow", f"pid:{process.pid}")
    wait(lambda: json.loads(run("hyprctl", "activewindow", "-j")).get("pid") == process.pid)
    wait(lambda: Path(str(path) + ".focus").exists() and Path(str(path) + ".focus").read_text() == "1")
    return process, path


def record(wav, command="start"):
    with wave.open(str(wav)) as audio:
        playback_timeout = max(40, audio.getnframes() / audio.getframerate() + 15)
    call("press" if hardware else command)
    phase("recording")
    if args.backend_switch:
        switch = subprocess.run([str(binary), "backend", "x-asr"], env=env, capture_output=True)
        check("backend switch is rejected while recording", switch.returncode != 0)
    playback = ["paplay", "--device=" + (args.hardware_sink if hardware else node + "_sink")]
    if hardware:
        playback.append(f"--volume={args.hardware_volume}")
    playback.append(str(wav.resolve()))
    if command == "start" and not (out / "recording.png").exists():
        with subprocess.Popen(playback, env=env) as player:
            time.sleep(4)
            run("grim", str(out / "recording.png"))
            if player.wait(timeout=playback_timeout):
                raise RuntimeError("Public speech playback failed")
    else:
        subprocess.run(playback,
                       env=env, check=True, timeout=playback_timeout)
    time.sleep(0.25)
    call("release" if hardware else "stop")
    def completed():
        state = call("status")["state"]
        return state if state["phase"] in ("ready", "idle", "error") else None
    return wait(completed, 180 if args.continuity_only else 40)


def check(name, condition):
    if not condition:
        raise AssertionError(name)
    checks.append(name)
    print("PASS", name, flush=True)


try:
    keyboard = subprocess.Popen([str(root / "build/test_keyboard")], env=env,
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(keyboard)
    if keyboard.stdout.readline().strip() != "READY":
        raise RuntimeError("Test keyboard initialization failed")
    if not hardware:
        modules.append(run("pactl", "load-module", "module-null-sink", f"sink_name={node}_sink", "rate=16000", "channels=1").strip())
        modules.append(run("pactl", "load-module", "module-remap-source", f"master={node}_sink.monitor", f"source_name={node}_source").strip())
    if args.fcitx:
        profile = Path(env["XDG_CONFIG_HOME"]) / "fcitx5/profile"
        profile.parent.mkdir(parents=True, exist_ok=True)
        profile.write_text("[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=pinyin\n[Groups/0/Items/0]\nName=keyboard-us\n[Groups/0/Items/1]\nName=pinyin\n[GroupOrder]\n0=Default\n")
        spawn("fcitx5", "--disable", "vinput,notifications")
        time.sleep(2)
    daemon = spawn(str(binary), "serve")
    phase("idle")
    if args.backend_switch:
        run(str(binary), "backend", "fun")
        switched = phase("idle")
        check("real Fun model loads through live backend command", switched["backend"] == "fun")
        check("successful backend selection is persisted", json.loads(config_path.read_text())["asr"]["backend"] == "fun")
        config["asr"] = {"backend": "fun"}
    prefix = "我们正在开发 Hyprvoice，使用 Hyprland。" if args.context_only else ""
    process, path = editor("拼音测试" if args.fcitx else prefix)
    if args.context_only:
        key("ctrl End")
    if args.fcitx:
        wait(lambda: "program:hyprvoice-test-editor frontend:dbus" in run("gdbus", "call", "--session", "--dest", "org.fcitx.Fcitx5", "--object-path", "/controller", "--method", "org.fcitx.Fcitx.Controller1.DebugInfo"))
        key("ctrl a")
        key("ctrl space")
        key("type nihao")
        key("key space")
        wait(lambda: "你好" in path.read_text())
        check("Fcitx Pinyin commits Chinese while Hyprvoice is running", True)
        key("ctrl space")
        key("ctrl a")
        key("key BackSpace")
    if args.coexist_only:
        (out / "result.json").write_text(json.dumps({"checks": checks}, ensure_ascii=False, indent=2))
        raise SystemExit(0)
    state = record(args.speech)
    if hardware:
        (out / "capture-state.json").write_text(json.dumps(
            {"phase": state["phase"], "error": state["error"],
             "raw_chars": len(state["raw"]), "stream_volume": args.hardware_volume},
            ensure_ascii=False, indent=2))
        check("real microphone retains recognized speech after key release", bool(state["raw"]))
        check("real microphone speech passes VAD without confirmation", not state["error"])
        wait(lambda: bool(path.read_text()), 20)
        state = phase("idle")
        check("held-key release automatically pastes into isolated editor", path.read_text() == prefix + state["raw"])
        # Locate the beginning, interior and end of both utterances. Exact
        # spelling accuracy is measured separately; a synonym/misrecognition
        # such as 河道 for 河段 is not evidence of a lost recording section.
        for marker in ("因为远离大陆", "哺乳动物", "亚马逊河", "宽度可达"):
            check("hardware acoustic recording retains " + marker, marker in path.read_text())
        cases = [json.loads(line) for line in
                 (args.speech.parent / "manifest.jsonl").read_text().splitlines()]
        reference = next(c["reference"] for c in cases
                         if Path(c["audio"]).resolve() == args.speech.resolve())
        expected, actual = normalized(reference), normalized(path.read_text())
        run("grim", str(out / "committed.png"))
        (out / "result.json").write_text(json.dumps(
            {"checks": checks, "state": state, "editor_text": path.read_text(),
             "hardware": True, "stream_volume": args.hardware_volume,
             "accuracy": {"reference": reference,
                          "characters": len(expected),
                          "character_edits": distance(expected, actual),
                          "cer": distance(expected, actual) / len(expected)}},
            ensure_ascii=False, indent=2))
        raise SystemExit(0)
    if args.backend_switch:
        switch = subprocess.run([str(binary), "backend", "x-asr"], env=env, capture_output=True)
        check("backend switch preserves pending preview", switch.returncode != 0 and call("status")["state"]["text"] == state["text"])
    check("real PipeWire speech produces a final transcript", state["phase"] == "ready" and bool(state["text"]))
    check("preview does not steal editor focus", json.loads(run("hyprctl", "activewindow", "-j"))["pid"] == process.pid)
    run("grim", str(out / "preview.png"))
    if args.context_only:
        check("production recording reads actual editor prefix", state["context"] == prefix)
        check("speech beginning survives context lookup",
              "因为远离大陆" in state["raw"] if args.continuity_only
              else state["raw"].startswith("报告"))
        if args.llm_env:
            check("actual editor context and transcript reach real model",
                  not state["error"] and bool(state["text"]) and path.read_text() == prefix)
        else:
            check("unconfigured context model requires confirmation instead of auto-paste",
                  bool(state["error"]) and path.read_text() == prefix)
    if args.vad_reject_only:
        time.sleep(1)
        check("negative VAD retains visible speech and confirmation warning",
              bool(state["raw"]) and "语音检测未确认讲话" in state["error"])
        check("uncertain speech never auto-pastes", path.read_text() == prefix)
        run("grim", str(out / "warning.png"))
    call("raw" if args.context_only and not args.llm_env else "commit")
    wait(lambda: path.read_text() == prefix + state["text"])
    check("UTF-8 transcript is pasted into real GTK Wayland editor", True)
    original = path.read_text()
    if args.continuity_only:
        # Both public sentences must survive, including the weak leading one.
        for marker in ("因为远离大陆", "哺乳动物", "亚马逊河", "河段"):
            check("continuous recording retains " + marker, marker in original)
        (out / "result.json").write_text(json.dumps(
            {"checks": checks, "state": state, "editor_text": original},
            ensure_ascii=False, indent=2))
        raise SystemExit(0)
    if args.backend_switch:
        call("start")
        phase("recording")
        run("paplay", f"--device={node}_sink", str(args.speech.resolve()))
        time.sleep(0.25)
        call("stop")
        phase("finalizing")
        started = time.monotonic()
        call("cancel")
        phase("idle")
        check("cancel during real Fun decoding stops promptly without paste", time.monotonic() - started < 2 and path.read_text() == original)
    if args.context_only:
        check("commit clears context from idle state", call("status")["state"]["context"] == "")
        call("start")
        phase("recording")
        call("cancel")
        phase("idle")
        check("cancel clears context and keeps editor text", call("status")["state"]["context"] == "" and path.read_text() == original)
        if args.llm_env:
            call("quit")
            daemon.wait(timeout=10)
            config["auto_commit"] = True
            config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2))
            daemon = spawn(str(binary), "serve")
            phase("idle")
            run("hyprctl", "dispatch", "focuswindow", f"pid:{process.pid}")
            auto = record(args.speech)
            wait(lambda: path.read_text() != original)
            # The worker can expose ready just before the GTK timer pastes.
            # Assert the observed post-paste state rather than that snapshot.
            auto = phase("idle")
            check("contextual auto-commit appends only new dictation once",
                  auto["phase"] == "idle" and not auto["error"]
                  and path.read_text().startswith(original)
                  and path.read_text().count(prefix) == 1
                  and path.read_text().count("报告") == 2)
            check("contextual auto-commit clears input context",
                  call("status")["state"]["context"] == "")
    if args.visual_only or args.context_only:
        (out / "result.json").write_text(json.dumps({"checks": checks, "transcript": state["text"]}, ensure_ascii=False, indent=2))
        raise SystemExit(0)
    noise = record(args.noise)
    check("pure noise produces no pending result", noise["phase"] == "idle" and not noise["raw"])
    check("pure noise does not insert text", path.read_text() == original)
    call("start")
    phase("recording")
    call("cancel")
    phase("idle")
    check("cancel leaves editor unchanged", path.read_text() == original)
    state = record(args.speech)
    other, other_path = editor("另一个窗口")
    reply = subprocess.run([str(binary), "commit"], env=env, capture_output=True, text=True)
    check("commit is rejected after target window changes", reply.returncode != 0)
    check("other window receives no transcript", other_path.read_text() == "另一个窗口")
    run("hyprctl", "dispatch", "focuswindow", f"pid:{process.pid}")
    call("cancel")
    x11, x11_path = editor(backend="x11")
    state = record(args.speech)
    call("commit")
    wait(lambda: x11_path.read_text() == state["text"])
    check("UTF-8 transcript is pasted into real GTK XWayland editor", True)
    terminal_path = out / "terminal.txt"
    terminal_path.write_text("")
    terminal = spawn("kitty", "--class", "hyprvoice-qa-terminal", "--title", "Hyprvoice terminal acceptance",
                     "python3", str(root / "tests/terminal_receiver.py"), str(terminal_path))
    wait(lambda: any(c["pid"] == terminal.pid for c in json.loads(run("hyprctl", "clients", "-j"))))
    run("hyprctl", "dispatch", "focuswindow", f"pid:{terminal.pid}")
    state = record(args.speech)
    call("commit")
    wait(lambda: state["text"] in terminal_path.read_text())
    check("Ctrl+Shift+V delivers UTF-8 transcript to real Kitty terminal", True)
    reply = subprocess.run([str(binary), "command"], env=env, capture_output=True, text=True)
    check("terminal command-mode replacement is rejected", reply.returncode != 0)
    run("hyprctl", "dispatch", "focuswindow", f"pid:{process.pid}")
    command_wave = out / "command.wav"
    run("espeak-ng", "-v", "en-us", "-s", "140", "-w", str(out / "command-original.wav"), "Make this text shorter.")
    run("ffmpeg", "-y", "-loglevel", "error", "-i", str(out / "command-original.wav"), "-ar", "16000", "-ac", "1", str(command_wave))
    key("ctrl a")
    before = path.read_text()
    transformed = record(command_wave, "command")
    (out / "command-result.json").write_text(json.dumps(transformed, ensure_ascii=False, indent=2))
    if args.llm_env:
        check("selected-text voice instruction reaches real LLM", transformed["phase"] == "ready" and bool(transformed["text"]))
        check("command waits for explicit confirmation", path.read_text() == before)
        run("grim", str(out / "command-preview.png"))
        call("commit")
        wait(lambda: path.read_text() == transformed["text"])
        check("confirmed voice command replaces original selection", True)
        (out / "command-result.json").write_text(json.dumps(transformed, ensure_ascii=False, indent=2))
    else:
        check("command with unconfigured LLM preserves selected text", transformed["phase"] == "error" and path.read_text() == before)
        check("spoken instruction cannot be committed as replacement", subprocess.run([str(binary), "raw"], env=env, capture_output=True).returncode != 0)
        call("cancel")
    check("daemon remains alive", daemon.poll() is None)
    call("quit")
    daemon.wait(timeout=10)
    check("graceful shutdown removes control socket", not (Path(env["XDG_RUNTIME_DIR"]) / "hyprvoice/control.sock").exists())
    config["auto_commit"] = True
    config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2))
    daemon = spawn(str(binary), "serve")
    phase("idle")
    run("hyprctl", "dispatch", "focuswindow", f"pid:{process.pid}")
    key("ctrl a")
    key("key BackSpace")
    wait(lambda: path.read_text() == "")
    call("press")
    call("release")
    phase("recording")
    run("paplay", f"--device={node}_sink", str(args.speech.resolve()))
    time.sleep(0.25)
    call("press")
    call("release")
    phase("idle")
    wait(lambda: bool(path.read_text()))
    check("tap toggles recording and default auto-commit pastes once", True)
    first = path.read_text()
    check("duplicate commit is rejected", subprocess.run([str(binary), "commit"], env=env, capture_output=True).returncode != 0)
    call("start")
    phase("recording")
    run("hyprctl", "dispatch", "focuswindow", f"pid:{other.pid}")
    wait(lambda: json.loads(run("hyprctl", "activewindow", "-j"))["pid"] == other.pid)
    time.sleep(0.2)
    run("hyprctl", "dispatch", "focuswindow", f"pid:{process.pid}")
    run("paplay", f"--device={node}_sink", str(args.speech.resolve()))
    time.sleep(0.25)
    call("stop")
    pending = phase("ready")
    check("switching away and back disables automatic insertion", path.read_text() == first)
    call("commit")
    wait(lambda: path.read_text() == first + pending["text"])
    check("manual confirmation inserts preserved result", True)
    before_hold = path.read_text()
    call("press")
    phase("recording")
    run("paplay", f"--device={node}_sink", str(args.speech.resolve()))
    time.sleep(0.25)
    call("release")
    phase("idle")
    wait(lambda: path.read_text() != before_hold)
    check("hold-to-talk finishes and inserts on release", True)
    if args.backend_switch:
        run(str(binary), "backend", "x-asr")
        check("switching back loads the original recognizer", phase("idle")["backend"] == "x-asr")
        check("rollback selection persists without changing DeepSeek configuration",
              json.loads(config_path.read_text())["asr"]["backend"] == "x-asr"
              and json.loads(config_path.read_text())["llm"] == config["llm"])
    (out / "result.json").write_text(json.dumps({"checks": checks, "transcript": state["text"]}, ensure_ascii=False, indent=2))
finally:
    if children:
        subprocess.run([str(binary), "quit"], env=env, capture_output=True)
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=6)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
    for module in reversed(modules):
        subprocess.run(["pactl", "unload-module", module], env=env, capture_output=True)
