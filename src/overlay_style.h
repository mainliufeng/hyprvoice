#pragma once
namespace hv {
inline constexpr const char *OverlayStyle = R"css(
window.hyprvoice { background: transparent; color: #202c3c; border: none; box-shadow: none; }
.voice-panel { background: #ffffff; border: 1px solid #dde3ec; border-radius: 18px; box-shadow: 0 8px 22px rgba(25,43,70,0.12); }
.voice-panel label, .voice-panel button, .voice-editor { font-family: "Noto Sans CJK SC", sans-serif; }
.voice-heading { font-size: 13px; font-weight: 600; color: #263750; }
.voice-status { font-size: 12px; color: #617086; }
.voice-status.attention { color: #805a0c; }
.voice-time { font-size: 12px; color: #617086; }
.voice-transcript { font-size: 17px; font-weight: 400; line-height: 28px; color: #202c3c; }
.voice-transcript.placeholder { color: #697789; }
.voice-hint, .voice-target, .voice-source { font-size: 12px; line-height: 20px; color: #617086; }
.voice-warning { font-size: 12px; line-height: 20px; color: #79591b; background: #fff7e7; border: 1px solid #ecd5a0; border-radius: 9px; padding: 10px 12px; }
.voice-instruction { font-size: 13px; line-height: 22px; color: #30488f; background: #edf1fb; border-left: 2px solid #4059a8; border-radius: 0 6px 6px 0; padding: 10px 12px; }
.voice-panel button { font-size: 12px; font-weight: 400; color: #53667e; background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 8px 9px; min-height: 20px; box-shadow: none; }
.voice-panel button:hover { background: #f0f3f8; color: #263750; }
.voice-panel button:focus-visible { outline: 2px solid #728ad0; outline-offset: 2px; }
.voice-panel button.primary { color: #fff; background: #4059a8; border-color: #4059a8; padding: 8px 14px; min-width: 84px; }
.voice-panel button.primary:hover { background: #30488f; }
.voice-panel button:disabled { opacity: 0.43; }
.voice-panel button.icon-only { min-width: 18px; padding: 8px; }
.voice-panel button.small { padding: 3px 7px; min-height: 20px; }
.voice-panel button.choice { border: 1px solid #dde3ec; border-radius: 8px; padding: 8px 10px; }
.voice-panel button.choice.current { background: #edf1fb; }
.voice-panel button.choice label { font-size: 17px; line-height: 26px; }
.voice-choice-panel { border: 1px solid #dde3ec; border-radius: 10px; padding: 8px; }
.voice-divider { background: #dde3ec; min-height: 1px; }
.voice-scroll { background: transparent; border: none; }
.voice-scroll scrollbar { background: transparent; }
.voice-scroll scrollbar slider { min-width: 6px; min-height: 24px; border: none; border-radius: 8px; background: #cad2df; margin: 2px; }
.voice-editor { font-size: 17px; font-weight: 400; color: #202c3c; background: #f4f6fa; border: 1px solid #cbd5e5; border-radius: 10px; padding: 10px 12px; }
.voice-editor:focus { border-color: #4059a8; outline: 2px solid #e3eafa; }
.voice-editor text { background: transparent; color: #202c3c; }
)css";
}
