#include "tray.h"

#include <array>
#include <cstring>
#include <exception>
#include <utility>

namespace hv {
namespace {

constexpr char item_path[] = "/StatusNotifierItem";
constexpr char menu_path[] = "/MenuBar";
constexpr char watcher_name[] = "org.kde.StatusNotifierWatcher";
constexpr char item_interface[] = "org.kde.StatusNotifierItem";
constexpr char menu_interface[] = "com.canonical.dbusmenu";
constexpr guint layout_revision = 1;

constexpr char introspection_xml[] = R"xml(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="u" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <method name="Activate"><arg type="i"/><arg type="i"/></method>
    <method name="ContextMenu"><arg type="i"/><arg type="i"/></method>
    <method name="SecondaryActivate"><arg type="i"/><arg type="i"/></method>
    <method name="Scroll"><arg type="i"/><arg type="s"/></method>
  </interface>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg type="i"/><arg type="i"/><arg type="as"/>
      <arg type="u" direction="out"/>
      <arg type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg type="ai"/><arg type="as"/>
      <arg type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg type="i"/><arg type="s"/><arg type="v" direction="out"/>
    </method>
    <method name="Event"><arg type="i"/><arg type="s"/><arg type="v"/><arg type="u"/></method>
    <method name="EventGroup"><arg type="a(isvu)"/><arg type="ai" direction="out"/></method>
    <method name="AboutToShow"><arg type="i"/><arg type="b" direction="out"/></method>
    <method name="AboutToShowGroup">
      <arg type="ai"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})"/><arg type="a(ias)"/></signal>
  </interface>
</node>)xml";

struct MenuItem {
  const char *label;
  TrayAction action;
};

constexpr MenuItem menu_items[] = {
    {"极简模式", TrayAction::Simple},
    {"详细模式", TrayAction::Detailed},
    {"设置…", TrayAction::Settings},
    {"退出", TrayAction::Quit},
};

constexpr int root_id = 0;
constexpr int simple_id = 1;
constexpr int detailed_id = 2;
constexpr int settings_id = 3;
constexpr int last_id = sizeof(menu_items) / sizeof(menu_items[0]);

bool valid_id(int id) { return id >= root_id && id <= last_id; }

GVariant *microphone_pixmap() {
  constexpr int size = 24;
  constexpr char mask[size][size + 1] = {
      "........................", "........................",
      "..........####..........", ".........#++++#.........",
      "........#++++++#........", "........#++++++#........",
      "........#++++++#........", "........#++++++#........",
      "........#++++++#........", "....###.#++++++#.###....",
      "....#+#.#++++++#.#+#....", "....#+#.#++++++#.#+#....",
      "....#+#.#++++++#.#+#....", "....#++#.#++++#.#++#....",
      ".....#++#.####.#++#.....", ".....#++++#..#++++#.....",
      "......##++++++++##......", "........##++++##........",
      "..........#++#..........", "..........#++#..........",
      ".......#++++++++#.......", ".......##########.......",
      "........................", "........................",
  };
  // SNI uses ARGB32 in network byte order. The outline stays visible on light
  // bars and the fill stays visible on dark bars, independent of the icon
  // theme.
  static constexpr auto pixels = [&] {
    std::array<guint8, size * size * 4> data{};
    size_t offset = 0;
    for (const auto &row : mask) {
      for (int x = 0; x < size; ++x) {
        const char pixel = row[x];
        const guint8 shade = pixel == '+' ? 245 : 32;
        if (pixel != '.') {
          data[offset] = 255;
          data[offset + 1] = shade;
          data[offset + 2] = shade;
          data[offset + 3] = shade;
        }
        offset += 4;
      }
    }
    return data;
  }();
  auto *bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, pixels.data(),
                                          pixels.size(), sizeof(guint8));
  auto *image = g_variant_new("(ii@ay)", size, size, bytes);
  return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), &image, 1);
}

GVariant *property(const char *interface, const char *name) {
  if (std::strcmp(interface, item_interface) == 0) {
    if (std::strcmp(name, "Category") == 0)
      return g_variant_new_string("ApplicationStatus");
    if (std::strcmp(name, "Id") == 0)
      return g_variant_new_string("hyprvoice");
    if (std::strcmp(name, "Title") == 0)
      return g_variant_new_string("HyprVoice");
    if (std::strcmp(name, "Status") == 0)
      return g_variant_new_string("Active");
    if (std::strcmp(name, "WindowId") == 0)
      return g_variant_new_uint32(0);
    if (std::strcmp(name, "IconName") == 0)
      return g_variant_new_string("");
    if (std::strcmp(name, "IconPixmap") == 0)
      return microphone_pixmap();
    if (std::strcmp(name, "ToolTip") == 0)
      return g_variant_new("(sa(iiay)ss)", "", nullptr, "HyprVoice", "");
    if (std::strcmp(name, "Menu") == 0)
      return g_variant_new_object_path(menu_path);
    if (std::strcmp(name, "ItemIsMenu") == 0)
      return g_variant_new_boolean(false);
  } else {
    if (std::strcmp(name, "Version") == 0)
      return g_variant_new_uint32(4);
    if (std::strcmp(name, "TextDirection") == 0)
      return g_variant_new_string("ltr");
    if (std::strcmp(name, "Status") == 0)
      return g_variant_new_string("normal");
    if (std::strcmp(name, "IconThemePath") == 0)
      return g_variant_new_strv(nullptr, 0);
  }
  return nullptr;
}

bool requested(GVariant *names, const char *name) {
  if (!names || g_variant_n_children(names) == 0)
    return true;
  GVariantIter iter;
  const char *requested_name;
  g_variant_iter_init(&iter, names);
  while (g_variant_iter_next(&iter, "&s", &requested_name)) {
    if (std::strcmp(requested_name, name) == 0)
      return true;
  }
  return false;
}

void invalid_id(GDBusMethodInvocation *invocation) {
  g_dbus_method_invocation_return_error_literal(
      invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Unknown menu item");
}

} // namespace

Tray::Tray(GDBusConnection *connection, bool detailed,
           std::function<void(TrayAction)> action)
    : connection_(connection ? G_DBUS_CONNECTION(g_object_ref(connection))
                             : nullptr),
      detailed_(detailed), action_(std::move(action)) {
  if (!connection_)
    return;

  static const GDBusInterfaceVTable vtable = {
      [](GDBusConnection *, const char *, const char *, const char *interface,
         const char *method, GVariant *parameters,
         GDBusMethodInvocation *invocation, gpointer data) {
        static_cast<Tray *>(data)->method_call(interface, method, parameters,
                                               invocation);
      },
      [](GDBusConnection *, const char *, const char *, const char *interface,
         const char *name, GError **,
         gpointer) -> GVariant * { return property(interface, name); },
      nullptr,
      {},
  };

  GError *error = nullptr;
  auto *node = g_dbus_node_info_new_for_xml(introspection_xml, &error);
  if (node) {
    item_id_ = g_dbus_connection_register_object(
        connection_, item_path,
        g_dbus_node_info_lookup_interface(node, item_interface), &vtable, this,
        nullptr, &error);
    if (item_id_) {
      menu_id_ = g_dbus_connection_register_object(
          connection_, menu_path,
          g_dbus_node_info_lookup_interface(node, menu_interface), &vtable,
          this, nullptr, &error);
    }
    g_dbus_node_info_unref(node);
  }
  if (error) {
    g_warning("HyprVoice tray: %s", error->message);
    g_error_free(error);
    return;
  }

  // Hosts can start after HyprVoice and can replace their watcher on restart.
  watcher_id_ = g_bus_watch_name_on_connection(
      connection_, watcher_name, G_BUS_NAME_WATCHER_FLAGS_NONE,
      [](GDBusConnection *bus, const char *, const char *owner, gpointer) {
        g_dbus_connection_call(
            bus, owner, "/StatusNotifierWatcher", watcher_name,
            "RegisterStatusNotifierItem", g_variant_new("(s)", item_path),
            G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, nullptr,
            [](GObject *source, GAsyncResult *result, gpointer) {
              GError *error = nullptr;
              if (auto *reply = g_dbus_connection_call_finish(
                      G_DBUS_CONNECTION(source), result, &error)) {
                g_variant_unref(reply);
              } else {
                g_debug("HyprVoice tray registration: %s", error->message);
                g_error_free(error);
              }
            },
            nullptr);
      },
      nullptr, nullptr, nullptr);
}

Tray::~Tray() {
  if (watcher_id_)
    g_bus_unwatch_name(watcher_id_);
  if (menu_id_)
    g_dbus_connection_unregister_object(connection_, menu_id_);
  if (item_id_)
    g_dbus_connection_unregister_object(connection_, item_id_);
  g_clear_object(&connection_);
}

GVariant *Tray::menu_properties(int id, GVariant *names) const {
  GVariantBuilder values;
  g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
  if (id == root_id) {
    if (requested(names, "children-display"))
      g_variant_builder_add(&values, "{sv}", "children-display",
                            g_variant_new_string("submenu"));
    return g_variant_builder_end(&values);
  }
  if (requested(names, "label"))
    g_variant_builder_add(&values, "{sv}", "label",
                          g_variant_new_string(menu_items[id - 1].label));
  if (requested(names, "enabled"))
    g_variant_builder_add(&values, "{sv}", "enabled",
                          g_variant_new_boolean(menu_item_enabled(id)));
  if (requested(names, "visible"))
    g_variant_builder_add(&values, "{sv}", "visible",
                          g_variant_new_boolean(true));
  if (id == simple_id || id == detailed_id) {
    if (requested(names, "toggle-type"))
      g_variant_builder_add(&values, "{sv}", "toggle-type",
                            g_variant_new_string("radio"));
    if (requested(names, "toggle-state"))
      g_variant_builder_add(
          &values, "{sv}", "toggle-state",
          g_variant_new_int32((id == detailed_id) == detailed_));
  }
  return g_variant_builder_end(&values);
}

bool Tray::menu_item_enabled(int id) const {
  if (id == simple_id || id == detailed_id)
    return mode_enabled_;
  if (id == settings_id)
    return settings_enabled_;
  return id != root_id;
}

bool Tray::click_menu_item(int id) {
  if (!menu_item_enabled(id))
    return false;
  const auto action = menu_items[id - 1].action;
  const bool quitting = action == TrayAction::Quit;
  // A clean exit must still deliver the click reply to the host.
  if (quitting)
    g_dbus_connection_flush_sync(connection_, nullptr, nullptr);
  run_action(action);
  return quitting;
}

void Tray::run_action(TrayAction action) noexcept {
  try {
    action_(action);
  } catch (const std::exception &error) {
    g_warning("HyprVoice tray action: %s", error.what());
  } catch (...) {
    g_warning("HyprVoice tray action failed");
  }
}

GVariant *Tray::menu_layout(int id, int depth, GVariant *names) const {
  GVariantBuilder children;
  g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
  if (id == root_id && depth != 0) {
    for (int child = simple_id; child <= last_id; ++child)
      g_variant_builder_add(&children, "v", menu_layout(child, 0, names));
  }
  return g_variant_new("(i@a{sv}av)", id, menu_properties(id, names),
                       &children);
}

void Tray::set_state(bool detailed, bool mode_enabled, bool settings_enabled) {
  const bool toggle_changed = detailed != detailed_;
  const bool mode_changed = mode_enabled != mode_enabled_;
  const bool settings_changed = settings_enabled != settings_enabled_;
  if (!toggle_changed && !mode_changed && !settings_changed)
    return;
  detailed_ = detailed;
  mode_enabled_ = mode_enabled;
  settings_enabled_ = settings_enabled;
  if (!menu_id_)
    return;

  GVariantBuilder updated;
  g_variant_builder_init(&updated, G_VARIANT_TYPE("a(ia{sv})"));
  if (toggle_changed || mode_changed) {
    const char *names[2];
    gsize count = 0;
    if (toggle_changed)
      names[count++] = "toggle-state";
    if (mode_changed)
      names[count++] = "enabled";
    auto *properties = g_variant_ref_sink(g_variant_new_strv(names, count));
    for (int id : {simple_id, detailed_id})
      g_variant_builder_add(&updated, "(i@a{sv})", id,
                            menu_properties(id, properties));
    g_variant_unref(properties);
  }
  if (settings_changed) {
    GVariantBuilder values;
    g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&values, "{sv}", "enabled",
                          g_variant_new_boolean(settings_enabled_));
    g_variant_builder_add(&updated, "(ia{sv})", settings_id, &values);
  }
  g_dbus_connection_emit_signal(
      connection_, nullptr, menu_path, menu_interface, "ItemsPropertiesUpdated",
      g_variant_new("(a(ia{sv})a(ias))", &updated, nullptr), nullptr);
}

void Tray::method_call(const char *interface, const char *method,
                       GVariant *parameters,
                       GDBusMethodInvocation *invocation) {
  if (std::strcmp(interface, item_interface) == 0) {
    g_dbus_method_invocation_return_value(invocation, nullptr);
    if (std::strcmp(method, "Activate") == 0 && settings_enabled_)
      run_action(TrayAction::Settings);
    return;
  }

  if (std::strcmp(method, "GetLayout") == 0) {
    int id, depth;
    GVariant *names;
    g_variant_get(parameters, "(ii@as)", &id, &depth, &names);
    if (valid_id(id)) {
      g_dbus_method_invocation_return_value(
          invocation, g_variant_new("(u@(ia{sv}av))", layout_revision,
                                    menu_layout(id, depth, names)));
    } else {
      invalid_id(invocation);
    }
    g_variant_unref(names);
    return;
  }
  if (std::strcmp(method, "GetGroupProperties") == 0) {
    GVariantIter *ids;
    GVariant *names;
    g_variant_get(parameters, "(ai@as)", &ids, &names);
    GVariantBuilder values;
    g_variant_builder_init(&values, G_VARIANT_TYPE("a(ia{sv})"));
    int id;
    if (g_variant_iter_n_children(ids) == 0) {
      for (id = root_id; id <= last_id; ++id)
        g_variant_builder_add(&values, "(i@a{sv})", id,
                              menu_properties(id, names));
    }
    while (g_variant_iter_next(ids, "i", &id)) {
      if (valid_id(id))
        g_variant_builder_add(&values, "(i@a{sv})", id,
                              menu_properties(id, names));
    }
    g_variant_iter_free(ids);
    g_variant_unref(names);
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(a(ia{sv}))", &values));
    return;
  }
  if (std::strcmp(method, "GetProperty") == 0) {
    int id;
    const char *name;
    g_variant_get(parameters, "(i&s)", &id, &name);
    if (!valid_id(id)) {
      invalid_id(invocation);
      return;
    }
    auto *properties = g_variant_ref_sink(menu_properties(id, nullptr));
    auto *value = g_variant_lookup_value(properties, name, nullptr);
    if (value) {
      g_dbus_method_invocation_return_value(invocation,
                                            g_variant_new("(v)", value));
      g_variant_unref(value);
    } else {
      g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR,
                                                    G_DBUS_ERROR_INVALID_ARGS,
                                                    "Unknown menu property");
    }
    g_variant_unref(properties);
    return;
  }
  if (std::strcmp(method, "Event") == 0) {
    int id;
    const char *event;
    g_variant_get(parameters, "(i&s*u)", &id, &event, nullptr, nullptr);
    if (!valid_id(id)) {
      invalid_id(invocation);
      return;
    }
    const bool clicked = std::strcmp(event, "clicked") == 0;
    g_dbus_method_invocation_return_value(invocation, nullptr);
    if (clicked)
      click_menu_item(id);
    return;
  }
  if (std::strcmp(method, "EventGroup") == 0) {
    auto *events = g_variant_get_child_value(parameters, 0);
    GVariantIter iter;
    GVariantBuilder errors;
    g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
    int id;
    g_variant_iter_init(&iter, events);
    while (
        g_variant_iter_next(&iter, "(i&s*u)", &id, nullptr, nullptr, nullptr)) {
      if (!valid_id(id))
        g_variant_builder_add(&errors, "i", id);
    }
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(ai)", &errors));
    const char *event;
    g_variant_iter_init(&iter, events);
    while (
        g_variant_iter_next(&iter, "(i&s*u)", &id, &event, nullptr, nullptr)) {
      if (valid_id(id) && std::strcmp(event, "clicked") == 0 &&
          click_menu_item(id))
        break;
    }
    g_variant_unref(events);
    return;
  }
  if (std::strcmp(method, "AboutToShow") == 0) {
    int id;
    g_variant_get(parameters, "(i)", &id);
    if (valid_id(id))
      g_dbus_method_invocation_return_value(invocation,
                                            g_variant_new("(b)", false));
    else
      invalid_id(invocation);
    return;
  }
  if (std::strcmp(method, "AboutToShowGroup") == 0) {
    GVariantIter *ids;
    g_variant_get(parameters, "(ai)", &ids);
    GVariantBuilder errors;
    g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
    int id;
    while (g_variant_iter_next(ids, "i", &id)) {
      if (!valid_id(id))
        g_variant_builder_add(&errors, "i", id);
    }
    g_variant_iter_free(ids);
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(aiai)", nullptr, &errors));
    return;
  }
  g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR,
                                                G_DBUS_ERROR_UNKNOWN_METHOD,
                                                "Unknown tray method");
}

} // namespace hv
