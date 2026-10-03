#pragma once

namespace hv {
inline constexpr const char *OverlayStyle = R"css(
window.hyprvoice {
  background: transparent; color: #eef0f3;
  border: none; border-radius: 0; box-shadow: none;
}
.voice-panel {
  background: #202023;
  border: 1px solid #3e3e43;
  border-radius: 18px;
  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.32);
}
.voice-panel label { font-family: sans-serif; }
.voice-icon {
  color: #e4e4e7;
  background: #303034;
  border-radius: 10px;
  padding: 8px;
}
.voice-heading { font-size: 13px; font-weight: 600; color: #eef0f3; }
.voice-mode {
  font-size: 11px;
  font-weight: 500;
  color: #b4bac5;
  background: #303034;
  padding: 3px 8px;
  border-radius: 6px;
}
.voice-time { font-family: monospace; font-size: 12px; color: #a0a8b5; }
.voice-spinner { color: #e4e4e7; min-width: 16px; min-height: 16px; }
.voice-transcript { font-size: 16px; line-height: 1.5; color: #f0f2f5; }
.voice-transcript.placeholder { color: #848e9d; }
.voice-context { font-size: 11px; line-height: 1.4; color: #a1a1aa; background: #29292d; padding: 8px 10px; border-radius: 8px; }
.voice-hint { font-size: 11px; line-height: 1.4; color: #949dab; }
.voice-hint.warning { color: #e2bd98; background: transparent; border: none; box-shadow: none; }
.voice-panel button {
  font-size: 12px;
  font-weight: 500;
  color: #d4d9e1;
  background: #303034;
  background-image: none;
  border: 1px solid #414752;
  border-radius: 9px;
  padding: 7px 12px;
  min-height: 18px;
  box-shadow: none;
  text-shadow: none;
}
.voice-panel button:hover { background: #3c3c42; border-color: #596270; }
.voice-panel button:active { background: #27272b; }
.voice-panel button.primary {
  color: #202023;
  background: #e4e4e7;
  border-color: #e4e4e7;
  font-weight: 600;
}
.voice-panel button.primary:hover { background: #ffffff; border-color: #ffffff; }
.voice-panel button.primary:active { background: #c9c9ce; }
.voice-panel button.quiet { background: transparent; border-color: transparent; color: #a9b2c0; }
.voice-panel button.quiet:hover { background: #35353a; color: #eef0f3; }
.voice-panel button:disabled { opacity: 0.45; }
.voice-meter { padding: 0; border: none; min-height: 4px; }
.voice-meter trough {
  background: #36363c; border: none; border-radius: 3px;
  min-height: 4px; padding: 0; box-shadow: none;
}
.voice-meter block {
  border: none; border-radius: 3px; min-height: 4px;
  min-width: 0; padding: 0; margin: 0;
}
.voice-meter block.empty { background: #36363c; }
.voice-meter block.filled { background: #e4e4e7; }
)css";
} // namespace hv
