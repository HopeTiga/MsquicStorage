#include <iostream>
#include <string>
#include <boost/asio.hpp>

#include "wtf.h"

#include "MsquicWebTransportServer.h"
#include "ConfigManager.h"

#include "Utils.h"

int main() {

#ifdef _WIN32

    SetConsoleOutputCP(CP_UTF8);

    SetConsoleCP(CP_UTF8);
#endif

    ConfigManager::Instance().Load("config.ini", ConfigManager::Format::Ini);

    boost::asio::io_context ioContext;

    size_t port = ConfigManager::Instance().GetInt("MquicWebTransportServer.port");

    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(ioContext));

    hope::quic::MsquicWebTransportServer mquicWebTransportServer(port);

    if (!mquicWebTransportServer.initialize()) {

        return -1;

    }

    boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);

    signals.async_wait([&ioContext, &mquicWebTransportServer, &work](const boost::system::error_code& error, int signal) {

        mquicWebTransportServer.shutDown();

        work.reset();

        ioContext.stop();

        });

    ioContext.run();

    return 0;

}