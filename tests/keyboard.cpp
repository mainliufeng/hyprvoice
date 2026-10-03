// Persistent, standard US keyboard for a headless desktop acceptance session.
#include "virtual-keyboard.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
struct Keyboard {
  wl_seat *seat = nullptr;
  zwp_virtual_keyboard_manager_v1 *manager = nullptr;
  zwp_virtual_keyboard_v1 *keyboard = nullptr;
  xkb_keymap *map = nullptr;
  void key(xkb_keysym_t symbol) {
    for (auto code = xkb_keymap_min_keycode(map);
         code <= xkb_keymap_max_keycode(map); ++code) {
      const xkb_keysym_t *syms = nullptr;
      int n = xkb_keymap_key_get_syms_by_level(map, code, 0, 0, &syms);
      if (n > 0 && syms[0] == symbol) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
        zwp_virtual_keyboard_v1_key(keyboard, now, code - 8,
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(keyboard, now + 1, code - 8,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
        return;
      }
    }
    throw std::runtime_error("Unknown test key");
  }
};
int main() {
  try {
    auto display = wl_display_connect(nullptr);
    if (!display)
      throw std::runtime_error("No Wayland display");
    Keyboard k;
    auto registry = wl_display_get_registry(display);
    wl_registry_listener listener{
        +[](void *data, wl_registry *r, uint32_t id, const char *name,
            uint32_t version) {
          auto &k = *static_cast<Keyboard *>(data);
          if (std::strcmp(name, "wl_seat") == 0)
            k.seat = static_cast<wl_seat *>(wl_registry_bind(
                r, id, &wl_seat_interface, std::min(version, 7u)));
          if (std::strcmp(name, "zwp_virtual_keyboard_manager_v1") == 0)
            k.manager =
                static_cast<zwp_virtual_keyboard_manager_v1 *>(wl_registry_bind(
                    r, id, &zwp_virtual_keyboard_manager_v1_interface, 1));
        },
        +[](void *, wl_registry *, uint32_t) {}};
    wl_registry_add_listener(registry, &listener, &k);
    wl_display_roundtrip(display);
    if (!k.seat || !k.manager)
      throw std::runtime_error("Virtual keyboard unavailable");
    auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names{};
    names.layout = "us";
    k.map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!k.map)
      throw std::runtime_error("Cannot compile test keyboard map");
    auto text = xkb_keymap_get_as_string(k.map, XKB_KEYMAP_FORMAT_TEXT_V1);
    int fd = memfd_create("hyprvoice-test-keyboard", MFD_CLOEXEC);
    if (fd < 0)
      throw std::runtime_error("Cannot allocate test keyboard map");
    size_t size = std::strlen(text) + 1;
    if (write(fd, text, size) != static_cast<ssize_t>(size))
      throw std::runtime_error("Cannot write test keyboard map");
    k.keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        k.manager, k.seat);
    zwp_virtual_keyboard_v1_keymap(k.keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                   fd, size);
    close(fd);
    free(text);
    wl_display_roundtrip(display);
    std::cout << "READY" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.starts_with("type "))
        for (unsigned char ch : line.substr(5))
          k.key(ch);
      else if (line.starts_with("key "))
        k.key(xkb_keysym_from_name(line.substr(4).c_str(),
                                   XKB_KEYSYM_CASE_INSENSITIVE));
      else if (line.starts_with("ctrl ")) {
        auto mod = xkb_keymap_mod_get_index(k.map, XKB_MOD_NAME_CTRL);
        zwp_virtual_keyboard_v1_modifiers(k.keyboard, 1u << mod, 0, 0, 0);
        k.key(xkb_keysym_from_name(line.substr(5).c_str(),
                                   XKB_KEYSYM_CASE_INSENSITIVE));
        zwp_virtual_keyboard_v1_modifiers(k.keyboard, 0, 0, 0, 0);
      } else if (line == "quit")
        break;
      else
        throw std::runtime_error("Unknown test keyboard command");
      wl_display_roundtrip(display);
      std::cout << "OK" << std::endl;
    }
    zwp_virtual_keyboard_v1_destroy(k.keyboard);
    wl_display_roundtrip(display);
    xkb_keymap_unref(k.map);
    xkb_context_unref(ctx);
    wl_display_disconnect(display);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
