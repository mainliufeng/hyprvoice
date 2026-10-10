// Drive the production destination and paste code in an isolated compositor.
// The fixed text represents an already recognized transcript, not an ASR test.
#include "context.h"
#include "desktop.h"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "read-target") {
      std::cout << hv::ReadInputTarget(std::stoi(argv[2])).dump() << '\n';
      return 0;
    }
    if (argc < 3)
      return 2;
    hv::Config config(argv[1]);
    hv::Desktop desktop(config);
    std::string action = argv[2];
    if (action == "serve") {
      std::string line;
      while (std::getline(std::cin, line)) {
        try {
          auto request = hv::Json::parse(line);
          if (request.at("action") == "copy") {
            desktop.copy(request.at("text"), request.at("seat"));
          } else {
            std::ifstream input(request.at("path").get<std::string>());
            auto value = hv::Json::parse(input);
            hv::Target target{value.at("address"),
                              value.at("app"),
                              value.at("stable"),
                              value.at("pid"),
                              value.at("seat"),
                              value.at("token"),
                              value.value("kind", std::string("application"))};
            auto guard = value.at("guard");
            if (request.at("action") == "rebind-paste") {
              target = desktop.rebind(target);
              guard = desktop.inputTarget(target);
            } else if (request.at("action") != "paste")
              throw std::runtime_error("Unknown test action");
            desktop.paste(target, request.at("text"), guard, true);
          }
          std::cout << hv::Json{{"ok", true}}.dump() << std::endl;
        } catch (const std::exception &error) {
          std::cout << hv::Json{{"ok", false}, {"error", error.what()}}.dump()
                    << std::endl;
        }
      }
      return 0;
    }
    if (argc < 4)
      return 2;
    if (action == "capture") {
      const auto target = desktop.target();
      hv::Json value{
          {"address", target.address}, {"app", target.app},
          {"stable", target.stable},   {"pid", target.pid},
          {"seat", target.seat},       {"token", target.route_token},
          {"kind", target.kind},       {"guard", desktop.inputTarget(target)}};
      std::ofstream(argv[3]) << value.dump();
      std::cout << value.dump() << '\n';
    } else if (action == "paste" && argc == 5) {
      std::ifstream input(argv[3]);
      auto value = hv::Json::parse(input);
      hv::Target target{value.at("address"),
                        value.at("app"),
                        value.at("stable"),
                        value.at("pid"),
                        value.at("seat"),
                        value.at("token"),
                        value.value("kind", std::string("application"))};
      desktop.paste(target, argv[4], value.at("guard"), true);
      // Give the target's real clipboard reader time to finish before this
      // probe releases wl-copy ownership; production keeps its daemon alive.
      g_usleep(300000);
    } else
      return 2;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
