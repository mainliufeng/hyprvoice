#pragma once

#include <functional>
#include <gio/gio.h>

namespace hv {

enum class TrayAction { Simple, Detailed, Settings, Quit };

class Tray {
public:
  Tray(GDBusConnection *connection, bool detailed,
       std::function<void(TrayAction)> action);
  ~Tray();
  Tray(const Tray &) = delete;
  Tray &operator=(const Tray &) = delete;

  void set_state(bool detailed, bool mode_enabled, bool settings_enabled);

private:
  bool menu_item_enabled(int id) const;
  bool click_menu_item(int id);
  void run_action(TrayAction action) noexcept;
  GVariant *menu_properties(int id, GVariant *names) const;
  GVariant *menu_layout(int id, int depth, GVariant *names) const;
  void method_call(const char *interface, const char *method,
                   GVariant *parameters, GDBusMethodInvocation *invocation);

  GDBusConnection *connection_;
  bool detailed_;
  bool mode_enabled_ = true;
  bool settings_enabled_ = true;
  std::function<void(TrayAction)> action_;
  guint item_id_{}, menu_id_{}, watcher_id_{};
};

} // namespace hv
