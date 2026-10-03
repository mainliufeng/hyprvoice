-- Hyprland 0.55+: load from your config after installing hyprvoice.
-- Change these keys if they conflict with your existing bindings.
hl.bind("F8", hl.dsp.exec_cmd("hyprvoice press"))
hl.bind("F8", hl.dsp.exec_cmd("hyprvoice release"), { release = true })
hl.bind("F9", hl.dsp.exec_cmd("hyprvoice command"))
hl.bind("F9", hl.dsp.exec_cmd("hyprvoice stop"), { release = true })
hl.bind("SUPER + ALT + return", hl.dsp.exec_cmd("hyprvoice commit"))
hl.bind("SUPER + ALT + o", hl.dsp.exec_cmd("hyprvoice raw"))
hl.bind("SUPER + ALT + escape", hl.dsp.exec_cmd("hyprvoice cancel"))
hl.bind("SUPER + ALT + 1", hl.dsp.exec_cmd("hyprvoice scene raw"))
hl.bind("SUPER + ALT + 2", hl.dsp.exec_cmd("hyprvoice scene correct"))
hl.bind("SUPER + ALT + 3", hl.dsp.exec_cmd("hyprvoice scene format"))
hl.bind("SUPER + ALT + 4", hl.dsp.exec_cmd("hyprvoice scene translate"))
