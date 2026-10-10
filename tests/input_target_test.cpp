#include "context.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
hv::Json target() {
  return {{"available", true},
          {"protected", false},
          {"reliable", true},
          {"toolkit", "gtk"},
          {"control", {{"bus", ":1.42"}, {"path", "/test/entry"}}},
          {"caret", 8},
          {"selections", hv::Json::array()},
          {"selection_digests", hv::Json::array()},
          {"characters", 12},
          {"digest", std::string(64, 'a')}};
}
} // namespace

int main() {
  try {
    const auto original = target();
    check(hv::EquivalentInputTarget(original, original),
          "stable editable target must match");
    auto browser = original;
    browser["toolkit"] = "chromium";
    browser["editor_digest"] = std::string(64, 'c');
    check(hv::EquivalentInputTarget(browser, browser),
          "stable verified Chromium target must match");
    auto browser_changed = browser;
    browser_changed["digest"] = std::string(64, 'b');
    check(!hv::EquivalentInputTarget(browser, browser_changed),
          "browser equal-length edits must not match");
    browser_changed = browser;
    browser_changed["editor_digest"] = std::string(64, 'd');
    check(!hv::EquivalentInputTarget(browser, browser_changed),
          "browser embedded paragraph changes must not match");
    browser_changed = browser;
    browser_changed.erase("editor_digest");
    check(!hv::EquivalentInputTarget(browser_changed, browser_changed),
          "browser guard without embedded editor version must fail closed");
    browser_changed = browser;
    browser_changed["toolkit"] = "unknown";
    check(!hv::EquivalentInputTarget(browser_changed, browser_changed),
          "unverified toolkit must not match even identical snapshots");
    for (const auto &patch :
         {hv::Json{{"available", false}}, hv::Json{{"reliable", false}},
          hv::Json{{"protected", true}}, hv::Json{{"toolkit", "chromium"}},
          hv::Json{{"control", {{"bus", ":1.43"}, {"path", "/test/entry"}}}},
          hv::Json{{"control", {{"bus", ":1.42"}, {"path", "/test/other"}}}},
          hv::Json{{"caret", 9}},
          hv::Json{{"selections", hv::Json::array({{1, 4}})}},
          hv::Json{{"characters", 13}},
          hv::Json{{"digest", std::string(64, 'b')}}}) {
      auto changed = original;
      changed.update(patch);
      check(!hv::EquivalentInputTarget(original, changed) &&
                !hv::EquivalentInputTarget(changed, original),
            "changed or unsafe targets must not match");
    }
    auto selected = original;
    selected["selections"] = hv::Json::array({{2, 7}, {9, 11}});
    selected["selection_digests"] =
        hv::Json::array({std::string(64, 'b'), std::string(64, 'c')});
    check(hv::EquivalentInputTarget(selected, selected),
          "stable multiple selections must compare");
    auto shifted = selected;
    shifted["selections"] = hv::Json::array({{3, 8}, {9, 11}});
    check(!hv::EquivalentInputTarget(selected, shifted),
          "equal-length selection moved to another range must not match");
    auto reordered = selected;
    reordered["selections"] = hv::Json::array({{9, 11}, {2, 7}});
    check(!hv::EquivalentInputTarget(selected, reordered),
          "selection ordering changes must not match");
    auto changed_digest = selected;
    changed_digest["selection_digests"][0] = std::string(64, 'd');
    check(!hv::EquivalentInputTarget(selected, changed_digest),
          "selected text digest changes must not match");
    for (const auto &digests :
         {hv::Json::array(), hv::Json::array({"bad", "bad"}),
          hv::Json::array({42, std::string(64, 'b')})}) {
      auto invalid = selected;
      invalid["selection_digests"] = digests;
      check(!hv::EquivalentInputTarget(invalid, invalid),
            "missing or malformed selection digests must fail closed");
    }
    hv::Json local{{"available", true},
                   {"reliable", true},
                   {"protected", false},
                   {"toolkit", "wayland"},
                   {"route_token", "physical:2:3"},
                   {"revision", 4},
                   {"surroundingAvailable", false},
                   {"text", ""},
                   {"cursor", 0},
                   {"anchor", 0}};
    check(hv::EquivalentInputTarget(local, local),
          "empty native editor is guarded without pretending to have readable "
          "context");
    for (const auto &patch :
         {hv::Json{{"revision", 5}}, hv::Json{{"route_token", "physical:3:3"}},
          hv::Json{{"protected", true}},
          hv::Json{{"surroundingAvailable", true}}, hv::Json{{"text", "new"}},
          hv::Json{{"cursor", 0.5}}}) {
      auto changed = local;
      changed.update(patch);
      check(!hv::EquivalentInputTarget(local, changed),
            "native input movement and unsafe state must not match");
    }
    // Even two identical malformed snapshots must fail closed, without throws.
    for (const auto &patch :
         {hv::Json{{"reliable", "true"}},
          hv::Json{{"protected", nullptr}},
          hv::Json{{"caret", -1}},
          hv::Json{{"caret", 13}},
          hv::Json{{"caret", 8.5}},
          hv::Json{{"characters", -1}},
          hv::Json{{"caret", int64_t{4294967304}}},
          hv::Json{{"characters", int64_t{4294967308}}},
          hv::Json{{"characters", 65537}},
          hv::Json{{"characters", "12"}},
          hv::Json{{"control", {{"bus", ""}, {"path", "/test/entry"}}}},
          hv::Json{{"control", {{"bus", "not-unique"}, {"path", "relative"}}}},
          hv::Json{{"selections", "bad"}},
          hv::Json{{"selections", hv::Json::array({{1}})}},
          hv::Json{{"selections", hv::Json::array({{2, 1}})}},
          hv::Json{{"selections", hv::Json::array({{-1, 2}})}},
          hv::Json{{"selections", hv::Json::array({{1, 13}})}},
          hv::Json{{"selections", hv::Json::array({{"1", 2}})}},
          hv::Json{{"selections", hv::Json::array({{int64_t{4294967297},
                                                    int64_t{4294967298}}})}},
          hv::Json{{"digest", ""}},
          hv::Json{{"digest", std::string(64, 'z')}}}) {
      auto invalid = original;
      invalid.update(patch);
      check(!hv::EquivalentInputTarget(invalid, invalid),
            "identical malformed snapshots must not match");
    }
    for (const char *key :
         {"available", "protected", "reliable", "toolkit", "control", "caret",
          "selections", "selection_digests", "characters", "digest"}) {
      auto missing = original;
      missing.erase(key);
      check(!hv::EquivalentInputTarget(missing, missing),
            "missing snapshot fields must fail closed");
    }
    check(!hv::EquivalentInputTarget(hv::Json::array(), original),
          "invalid top-level snapshot must fail closed");
    auto unknown = hv::ReadInputTarget(0);
    check(hv::WindowOnlyInputTarget(unknown) &&
              !hv::EquivalentInputTarget(unknown, unknown),
          "missing accessibility permits window dictation, not editor proof");
    auto terminal = unknown;
    terminal["reason"] = "unsupported-role";
    terminal["terminal_window"] = true;
    check(hv::WindowOnlyInputTarget(terminal),
          "terminal role permits ordinary dictation without editor proof");
    for (const auto &patch :
         {hv::Json{{"protected", true}}, hv::Json{{"reliable", true}},
          hv::Json{{"selections", hv::Json::array({{1, 2}})}},
          hv::Json{{"reason", "unsupported-role"}},
          hv::Json{{"reason", "multiple-focus"}},
          hv::Json{{"reason", "text-read"}}, hv::Json{{"reason", "unstable"}},
          hv::Json{{"reason", "incomplete"}, {"stage", "browser-native-focus"}},
          hv::Json{{"control", {{"bus", ":1.42"}, {"path", "/editor"}}}}}) {
      auto blocked = unknown;
      blocked.update(patch);
      check(!hv::WindowOnlyInputTarget(blocked),
            "unsafe or discovered controls must not become window fallback");
    }
    check(!hv::WindowOnlyInputTarget(original),
          "verified editor cannot silently downgrade to window dictation");
    check(!unknown.at("available").get<bool>() &&
              !unknown.at("reliable").get<bool>() && !unknown.contains("text"),
          "invalid PID must not query accessibility or return input text");
    std::cout << "Input target comparison checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
