/**
 * @file main.cpp
 * @brief strict config 기반 nodus-vision executable entry point다.
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <boost/asio.hpp>

#include "vision_application.hpp"
#include "vision_config.hpp"

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: nodus-vision <config.json>\n";
        return 2;
    }
    try {
        std::ifstream input(argv[1]);
        if (!input) { throw std::runtime_error("Cannot open configuration file."); }
        std::ostringstream text;
        text << input.rdbuf();
        nodus_vision::VisionApplication application(nodus_vision::parseVisionConfig(text.str()));
        application.startApplication();
        boost::asio::io_context signals_context;
        boost::asio::signal_set signals(signals_context, SIGINT, SIGTERM);
        signals.async_wait([&application](const boost::system::error_code&, int) { application.stopApplication(); });
        signals_context.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "nodus-vision startup failed: " << error.what() << '\n';
        return 1;
    }
}
