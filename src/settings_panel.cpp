#include "settings_panel.h"
#include "diagnostics.h"
#include "ui_text.h"
namespace hv {
SettingsPanel::SettingsPanel(Action action) : action_(std::move(action)) {}
SettingsPanel::~SettingsPanel() {
  if (window_)
    gtk_window_destroy(GTK_WINDOW(window_));
}
bool SettingsPanel::visible() const {
  return window_ && gtk_widget_get_visible(window_);
}
void SettingsPanel::close() {
  if (saving_) {
    action_("settings-cancel", "");
    gtk_label_set_text(GTK_LABEL(message_),
                       "已请求取消；若保存已完成，可恢复上次设置");
  } else if (window_)
    gtk_widget_set_visible(window_, false);
}
void SettingsPanel::load(const Json &settings) {
  gtk_drop_down_set_selected(GTK_DROP_DOWN(backend_),
                             settings.at("asr").at("backend") == "fun" ? 1 : 0);
  auto chosen = settings.at("scene").get<std::string>();
  for (size_t i = 0; i < scenes_.size(); ++i)
    if (scenes_[i] == chosen)
      gtk_drop_down_set_selected(GTK_DROP_DOWN(scene_), i);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(automatic_),
                              settings.at("auto_commit").get<bool>());
  gtk_check_button_set_active(GTK_CHECK_BUTTON(context_),
                              settings.at("context").at("enabled").get<bool>());
}
void SettingsPanel::save() {
  auto index = gtk_drop_down_get_selected(GTK_DROP_DOWN(scene_));
  if (index >= scenes_.size())
    return;
  Json patch = {
      {"asr",
       {{"backend", gtk_drop_down_get_selected(GTK_DROP_DOWN(backend_)) == 1
                        ? "fun"
                        : "x-asr"}}},
      {"scene", scenes_[index]},
      {"auto_commit",
       gtk_check_button_get_active(GTK_CHECK_BUTTON(automatic_)) != 0},
      {"context",
       {{"enabled",
         gtk_check_button_get_active(GTK_CHECK_BUTTON(context_)) != 0}}}};
  auto result = action_("settings-save", patch.dump());
  if (result.value("ok", false))
    tick(true, "正在验证并保存设置…");
  else
    gtk_label_set_text(GTK_LABEL(message_),
                       result.value("error", std::string("未保存")).c_str());
}
void SettingsPanel::restore() {
  auto reply = action_("settings-previous", "");
  if (reply.value("ok", false)) {
    load(reply.at("settings"));
    gtk_label_set_text(GTK_LABEL(message_),
                       "上次设置已填回草稿；点击保存才会生效");
  } else
    gtk_label_set_text(
        GTK_LABEL(message_),
        reply.value("error", std::string("没有上次设置")).c_str());
}
void SettingsPanel::diagnose() {
  auto reply = action_("diagnose", "");
  gtk_label_set_text(GTK_LABEL(diagnostic_),
                     reply.value("ok", false)
                         ? DiagnosticText(reply.at("diagnostics")).c_str()
                         : "诊断未完成；未录音或联网");
}
void SettingsPanel::tick(bool saving, const std::string &message) {
  if (!window_)
    return;
  saving_ = saving;
  gtk_widget_set_sensitive(controls_, !saving);
  gtk_widget_set_sensitive(save_, !saving);
  gtk_widget_set_sensitive(restore_, !saving);
  gtk_widget_set_sensitive(refresh_, !saving);
  gtk_button_set_label(GTK_BUTTON(cancel_), saving ? "取消保存" : "取消／关闭");
  if (message != last_message_) {
    last_message_ = message;
    if (!message.empty())
      gtk_label_set_text(GTK_LABEL(message_), message.c_str());
  }
}
void SettingsPanel::show(const Config &config, const std::string &scene) {
  if (visible()) {
    gtk_window_present(GTK_WINDOW(window_));
    return;
  }
  if (window_)
    gtk_window_destroy(GTK_WINDOW(window_));
  saving_ = false;
  last_message_.clear();
  window_ = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(window_), "Hyprvoice 设置与诊断");
  gtk_window_set_default_size(GTK_WINDOW(window_), 460, 600);
  auto outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  for (auto margin :
       {GTK_POS_TOP, GTK_POS_BOTTOM, GTK_POS_LEFT, GTK_POS_RIGHT}) {
    if (margin == GTK_POS_TOP)
      gtk_widget_set_margin_top(outer, 20);
    else if (margin == GTK_POS_BOTTOM)
      gtk_widget_set_margin_bottom(outer, 20);
    else if (margin == GTK_POS_LEFT)
      gtk_widget_set_margin_start(outer, 20);
    else
      gtk_widget_set_margin_end(outer, 20);
  }
  gtk_window_set_child(GTK_WINDOW(window_), outer);
  auto heading = gtk_label_new("设置与诊断");
  gtk_widget_add_css_class(heading, "title-2");
  gtk_label_set_xalign(GTK_LABEL(heading), 0);
  gtk_box_append(GTK_BOX(outer), heading);
  auto note = gtk_label_new(
      "保存以下选项。麦克风和文本服务继续使用现有配置；不显示或编辑密钥。");
  gtk_label_set_wrap(GTK_LABEL(note), true);
  gtk_label_set_xalign(GTK_LABEL(note), 0);
  gtk_box_append(GTK_BOX(outer), note);
  controls_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_box_append(GTK_BOX(outer), controls_);
  auto dropdown = [&](const char *label, GtkStringList *list) {
    auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    auto name = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_widget_set_hexpand(name, true);
    gtk_box_append(GTK_BOX(row), name);
    auto choice = gtk_drop_down_new(G_LIST_MODEL(list), nullptr);
    gtk_accessible_update_property(GTK_ACCESSIBLE(choice),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    gtk_accessible_update_relation(GTK_ACCESSIBLE(choice),
                                   GTK_ACCESSIBLE_RELATION_LABELLED_BY, name,
                                   nullptr, -1);
    gtk_box_append(GTK_BOX(row), choice);
    gtk_box_append(GTK_BOX(controls_), row);
    return choice;
  };
  const char *backends[] = {"X-ASR", "Fun", nullptr};
  backend_ = dropdown("识别后端", gtk_string_list_new(backends));
  scenes_ = {"raw"};
  auto list = gtk_string_list_new(nullptr);
  gtk_string_list_append(list, SceneLabel("raw").c_str());
  for (const auto &[key, value] : config.data.at("prompts").items()) {
    if (key == "raw")
      continue;
    scenes_.push_back(key);
    gtk_string_list_append(list, SceneLabel(key).c_str());
  }
  scene_ = dropdown("文字场景", list);
  automatic_ =
      gtk_check_button_new_with_label("完成后自动插入（位置核验通过时）");
  context_ = gtk_check_button_new_with_label("前文辅助：会发送光标前已有文字");
  gtk_box_append(GTK_BOX(controls_), automatic_);
  gtk_box_append(GTK_BOX(controls_), context_);
  auto scope = gtk_label_new(
      ("前文上限沿用配置：" +
       std::to_string(config.data.at("context").at("max_chars").get<int>()) +
       " 个字符。选择处理场景会发送本次转录。")
          .c_str());
  gtk_label_set_wrap(GTK_LABEL(scope), true);
  gtk_label_set_xalign(GTK_LABEL(scope), 0);
  gtk_widget_add_css_class(scope, "dim-label");
  gtk_box_append(GTK_BOX(controls_), scope);
  auto settings = config.data;
  settings["scene"] = scene;
  load(settings);
  auto actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  auto button = [&](const char *label, auto callback) {
    auto b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", callback, this);
    gtk_box_append(GTK_BOX(actions), b);
    return b;
  };
  cancel_ = button("取消／关闭", G_CALLBACK(+[](GtkButton *, gpointer p) {
                     static_cast<SettingsPanel *>(p)->close();
                   }));
  restore_ = button("恢复上次设置", G_CALLBACK(+[](GtkButton *, gpointer p) {
                      static_cast<SettingsPanel *>(p)->restore();
                    }));
  save_ = button("保存设置", G_CALLBACK(+[](GtkButton *, gpointer p) {
                   static_cast<SettingsPanel *>(p)->save();
                 }));
  gtk_widget_add_css_class(save_, "suggested-action");
  gtk_box_append(GTK_BOX(outer), actions);
  message_ =
      gtk_label_new("关闭窗口会丢弃未保存草稿；录音或待确认时不能保存。");
  gtk_label_set_wrap(GTK_LABEL(message_), true);
  gtk_label_set_xalign(GTK_LABEL(message_), 0);
  gtk_box_append(GTK_BOX(outer), message_);
  gtk_box_append(GTK_BOX(outer), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  refresh_ = gtk_button_new_with_label("刷新本地诊断（不录音／不联网）");
  g_signal_connect(refresh_, "clicked",
                   G_CALLBACK(+[](GtkButton *, gpointer p) {
                     static_cast<SettingsPanel *>(p)->diagnose();
                   }),
                   this);
  gtk_box_append(GTK_BOX(outer), refresh_);
  auto scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, true);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 180);
  gtk_box_append(GTK_BOX(outer), scroll);
  diagnostic_ = gtk_label_new("点击刷新查看本地状态；不会试麦或联网。");
  gtk_label_set_wrap(GTK_LABEL(diagnostic_), true);
  gtk_label_set_xalign(GTK_LABEL(diagnostic_), 0);
  gtk_label_set_yalign(GTK_LABEL(diagnostic_), 0);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), diagnostic_);
  g_signal_connect(window_, "close-request",
                   G_CALLBACK(+[](GtkWindow *, gpointer p) -> gboolean {
                     static_cast<SettingsPanel *>(p)->close();
                     return true;
                   }),
                   this);
  auto keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed",
                   G_CALLBACK(+[](GtkEventControllerKey *, guint key, guint,
                                  GdkModifierType, gpointer p) -> gboolean {
                     if (key != GDK_KEY_Escape)
                       return false;
                     static_cast<SettingsPanel *>(p)->close();
                     return true;
                   }),
                   this);
  gtk_widget_add_controller(window_, keys);
  gtk_window_present(GTK_WINDOW(window_));
  gtk_widget_grab_focus(backend_);
}
} // namespace hv
