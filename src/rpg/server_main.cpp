#include "rpg/network.hpp"
#include "rpg/original_assets.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
static volatile std::sig_atomic_t stopped = 0;
int main(int argc, char **argv) {
  try {
    std::string bind = "127.0.0.1";
    std::filesystem::path assets = iron::rpg::originalAssetRoot();
    unsigned port = iron::rpg::defaultPort, seed = 1, ticks = 0;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto value = [&]() {
        if (++i >= argc)
          throw std::runtime_error("Missing " + a);
        return std::string(argv[i]);
      };
      if (a == "--bind")
        bind = value();
      else if (a == "--port")
        port = std::stoul(value());
      else if (a == "--seed")
        seed = std::stoul(value());
      else if (a == "--ticks")
        ticks = std::stoul(value());
      else if (a == "--assets")
        assets = value();
      else if (a == "--help") {
        std::cout << "mecha-hack-server [--bind IP] [--port 29607] [--seed N] "
                     "[--ticks N] [--assets PATH]\n";
        return 0;
      } else
        throw std::runtime_error("Unknown argument " + a);
    }
    if (port > 65535)
      throw std::runtime_error("Invalid port");
    iron::rpg::setOriginalAssetRoot(iron::rpg::gameAssetPath(assets));
    iron::rpg::Server server(bind, uint16_t(port), seed);
    std::signal(SIGINT, [](int) { stopped = 1; });
    std::signal(SIGTERM, [](int) { stopped = 1; });
    std::cout << "Mecha Hack UDP " << server.port() << " / seed " << seed
              << std::endl;
    auto deadline = std::chrono::steady_clock::now();
    for (unsigned i = 0; !stopped && (!ticks || i < ticks); ++i) {
      server.step();
      deadline += std::chrono::microseconds(16667);
      std::this_thread::sleep_until(deadline);
      if (std::chrono::steady_clock::now() - deadline > std::chrono::seconds(1))
        deadline = std::chrono::steady_clock::now();
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
