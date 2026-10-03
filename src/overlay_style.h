#pragma once

namespace hv {
inline constexpr const char *OverlayStyle = R"css(
window.hyprvoice {
  background: transparent; color: #eef0f3;
  border: none; border-radius: 0; box-shadow: none;
}
.voice-panel {
  background: #1d2026;
  border: 1px solid #393e47;
  border-radius: 18px;
  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.32);
}
.voice-panel label { font-family: sans-serif; }
.voice-icon {
  color: #a5e4ce;
  background: #2b3e3a;
  border-radius: 10px;
  padding: 8px;
}
.voice-heading { font-size: 13px; font-weight: 600; color: #eef0f3; }
.voice-mode {
  font-size: 11px;
  font-weight: 500;
  color: #b4bac5;
  background: #2b2f37;
  padding: 3px 8px;
  border-radius: 6px;
}
.voice-time { font-family: monospace; font-size: 12px; color: #a0a8b5; }
.voice-spinner { color: #a5e4ce; min-width: 16px; min-height: 16px; }
.voice-transcript { font-size: 16px; line-height: 1.5; color: #f0f2f5; }
.voice-transcript.placeholder { color: #848e9d; }
.voice-hint { font-size: 11px; line-height: 1.4; color: #949dab; }
.voice-hint.warning { color: #f0b7a4; }
.voice-panel button {
  font-size: 12px;
  font-weight: 500;
  color: #d4d9e1;
  background: #2b3039;
  background-image: none;
  border: 1px solid #414752;
  border-radius: 9px;
  padding: 7px 12px;
  min-height: 18px;
  box-shadow: none;
  text-shadow: none;
}
.voice-panel button:hover { background: #373e49; border-color: #596270; }
.voice-panel button:active { background: #222831; }
.voice-panel button.primary {
  color: #14251f;
  background: #a5e4ce;
  border-color: #a5e4ce;
  font-weight: 600;
}
.voice-panel button.primary:hover { background: #baf0dc; border-color: #baf0dc; }
.voice-panel button.primary:active { background: #8fcbb5; }
.voice-panel button.quiet { background: transparent; border-color: transparent; color: #a9b2c0; }
.voice-panel button.quiet:hover { background: #303640; color: #eef0f3; }
.voice-panel button:disabled { opacity: 0.45; }
.voice-meter { padding: 0; border: none; min-height: 4px; }
.voice-meter trough {
  background: #303841; border: none; border-radius: 3px;
  min-height: 4px; padding: 0; box-shadow: none;
}
.voice-meter block {
  border: none; border-radius: 3px; min-height: 4px;
  min-width: 0; padding: 0; margin: 0;
}
.voice-meter block.empty { background: #303841; }
.voice-meter block.filled { background: #a5e4ce; }
)css";
} // namespace hv
