#pragma once
#include "config.h"
#include <functional>
#include <gtk/gtk.h>
#include <vector>
namespace hv {
class SettingsPanel {
public:
  using Action = std::function<Json(const std::string &, const std::string &)>;
  explicit SettingsPanel(Action action);
  ~SettingsPanel();
  void show(const Config &config, const std::string &scene);
  void tick(bool saving, const std::string &message);
  bool visible() const;
  void close();

private:
  Action action_;
  GtkWidget *window_ = nullptr, *backend_ = nullptr, *scene_ = nullptr,
            *automatic_ = nullptr, *context_ = nullptr, *controls_ = nullptr,
            *message_ = nullptr, *diagnostic_ = nullptr, *save_ = nullptr,
            *restore_ = nullptr, *refresh_ = nullptr, *cancel_ = nullptr;
  std::vector<std::string> scenes_;
  bool saving_ = false;
  std::string last_message_;
  void load(const Json &settings);
  void save();
  void restore();
  void diagnose();
};
} // namespace hv
