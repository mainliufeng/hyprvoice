#pragma once

namespace hv {
inline constexpr const char *OverlayStyle = R"css(
window.hyprvoice {
  background: transparent; color: #222326;
  border: none; border-radius: 0; box-shadow: none;
}
.voice-panel {
  background: #fafafa;
  border: 1px solid rgba(255, 255, 255, 0.9);
  border-radius: 20px;
  box-shadow: 0 8px 28px rgba(0, 0, 0, 0.18), 0 1px 3px rgba(0, 0, 0, 0.1);
}
.voice-panel label { font-family: "Noto Sans CJK SC", sans-serif; }
.voice-icon { color: #303136; padding: 5px; }
.voice-heading { font-size: 14px; font-weight: 500; color: #45464c; }
.voice-status { font-size: 11px; color: #73747b; }
.voice-time { font-family: monospace; font-size: 11px; color: #73747b; }
.voice-spinner { color: #73747b; min-width: 14px; min-height: 14px; }
.voice-transcript { font-size: 17px; font-weight: 350; line-height: 1.7; color: #45464c; }
.voice-transcript.placeholder { color: #a0a1a7; }
.voice-context {
  font-size: 11px; line-height: 1.5; color: #73747b;
  border-left: 2px solid #dcdde1; padding: 0 0 0 10px;
}
.voice-hint { font-size: 11px; color: #73747b; }
.voice-warning {
  font-size: 12px; line-height: 1.5; color: #805b35;
  background: #f2ede6; border-radius: 10px; padding: 9px 12px;
}
.voice-panel button {
  font-size: 12px; font-weight: 500; color: #73747b;
  background: transparent; background-image: none;
  border: none; border-radius: 10px; padding: 8px 12px;
  min-height: 18px; box-shadow: none; text-shadow: none;
}
.voice-panel button:hover { background: #ececf0; color: #33343a; }
.voice-panel button:active { background: #e3e3e8; }
.voice-panel button.primary {
  color: #fafafa; background: #36373d; font-weight: 500;
  padding: 8px 16px;
}
.voice-panel button.primary:hover { background: #414248; }
.voice-panel button.primary:active { background: #15161a; }
.voice-panel button.close { padding: 5px; min-height: 18px; min-width: 18px; border-radius: 14px; }
.voice-panel button:disabled { opacity: 0.45; }
.voice-divider { min-height: 1px; background: #e9e9ed; }
.voice-scroll { background: transparent; border: none; }
.voice-scroll scrollbar { background: transparent; border: none; }
.voice-scroll scrollbar slider { background: #d6d6dd; min-width: 6px; min-height: 24px; border-radius: 3px; border: none; padding: 0; margin: 2px; }
)css";
} // namespace hv
