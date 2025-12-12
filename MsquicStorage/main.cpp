#include <iostream>
#include <string>
#include <boost/asio.hpp>

#include "MsquicServer.h"
#include "MsquicMysqlManagerPools.h"
#include "ConfigManager.h"

#include "Utils.h"

int main() {

#ifdef _WIN32

    SetConsoleOutputCP(CP_UTF8);

    SetConsoleCP(CP_UTF8);
#endif

    initLogger();

    setConsoleOutputLevels(0, 1, 1, 0);

    ConfigManager::Instance().Load("config.ini", ConfigManager::Format::Ini);

    boost::asio::io_context ioContext;

    size_t port = ConfigManager::Instance().GetInt("MsquicStorage.port");

    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(ioContext));

    hope::quic::MsquicServer msquicServer(port);

    hope::mysql::MsquicMysqlManagerPools::getInstance();

    if (!msquicServer.initialize()){
    
        LOG_ERROR("hope::quic::MsquicServer::initialize Failed");

        return -1;

    }

    if (!msquicServer.RunEventLoop()) {
    
        LOG_ERROR("hope::quic::MsquicServer::RunEventLoop Failed");

        return -1;

    }

    boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);

    signals.async_wait([&ioContext, &msquicServer, &work](const boost::system::error_code& error, int signal) {

        msquicServer.shutDown();

        work.reset();

        ioContext.stop();

        });

    ioContext.run();

    return 0;

}