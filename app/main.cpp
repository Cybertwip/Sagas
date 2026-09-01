#include <sagas/Engine.hpp>

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    try {
        sagas::ApplicationOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--assets" && i + 1 < argc) options.asset_root = argv[++i];
            else if (arg == "--title") options.start_at_title = true;
            else if (arg == "--menu") options.start_at_menu = true;
            else if (arg == "--headless") options.headless = true;
            else if (arg == "--frames" && i + 1 < argc) options.frame_limit = std::stoi(argv[++i]);
            else if (arg == "--capture" && i + 1 < argc) options.capture_path = argv[++i];
            else if (arg == "--help") {
                std::cout << "Sagas [--assets PATH] [--title|--menu] [--headless] [--frames N] [--capture PNG]\n";
                return 0;
            } else throw std::runtime_error("unknown argument: " + std::string(arg));
        }
        return sagas::Application(std::move(options)).run();
    } catch (const std::exception& error) {
        std::cerr << "Sagas: " << error.what() << '\n';
        return 1;
    }
}
