#pragma once
#include <memory>
#include <vector>
#include <thread>
#include <string>
#include <atomic>

#include <boost/asio.hpp>
#include <boost/mysql.hpp>
#include <boost/asio/steady_timer.hpp>

namespace hope {
    namespace mysql {

        class MsquicMysqlManager : public std::enable_shared_from_this<MsquicMysqlManager> {
        public:
            MsquicMysqlManager(boost::asio::io_context& ioContext);
            ~MsquicMysqlManager();

            void initConnection(std::string hostIP, size_t port, std::string username,
                std::string password, std::string database);

            std::shared_ptr<boost::mysql::any_connection> getConnection();

            void startHeartbeat(std::chrono::seconds interval = std::chrono::seconds(300)); // 默认5分钟
            void stopHeartbeat();

        private:
            void doHeartbeat();
            boost::asio::awaitable<void> executeHeartbeat();
            boost::asio::awaitable<bool> checkAndReconnect();

            boost::asio::io_context& ioContext;
            boost::asio::ssl::context sslContext;
            boost::asio::steady_timer heartbeatTimer;

            std::shared_ptr<boost::mysql::any_connection> mysqlConnection;

            std::string hostIP;
            size_t port;
            std::string username;
            std::string password;
            std::string database;

            std::chrono::seconds heartbeatInterval;
            std::atomic<bool> heartbeatRunning{ false };
            std::atomic<bool> isConnected{ false };
        };
    }
}