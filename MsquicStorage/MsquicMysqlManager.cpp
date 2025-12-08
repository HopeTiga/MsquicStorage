#include "MsquicMysqlManager.h"
#include "AsioProactors.h"
#include "Logger.h"
#include <iostream>

namespace hope {
    namespace mysql {
        MsquicMysqlManager::MsquicMysqlManager(boost::asio::io_context& ioContext)
            : sslContext(boost::asio::ssl::context::tls_client),
            ioContext(ioContext),
            heartbeatTimer(ioContext),
            heartbeatInterval(std::chrono::seconds(300)) { // 默认5分钟
        }

        MsquicMysqlManager::~MsquicMysqlManager() {
            stopHeartbeat();
        }

        void MsquicMysqlManager::initConnection(std::string hostIP, size_t port,
            std::string username, std::string password,
            std::string database) {
            this->hostIP = hostIP;
            this->port = port;
            this->username = username;
            this->password = password;
            this->database = database;

            boost::mysql::connect_params params;
            params.server_address = boost::mysql::host_and_port(hostIP, static_cast<unsigned short>(port));
            params.username = username;
            params.password = password;
            params.database = database;
            params.ssl = boost::mysql::ssl_mode::disable;

            mysqlConnection = std::make_shared<boost::mysql::any_connection>(ioContext);

            boost::asio::co_spawn(ioContext, [weak_self = std::weak_ptr<MsquicMysqlManager>(shared_from_this()), params]() -> boost::asio::awaitable<void> {
                // 尝试将weak_ptr提升为shared_ptr
                if (auto self = weak_self.lock()) {
                    try {
                        co_await self->mysqlConnection->async_connect(params);

                        self->isConnected = true;

                        Logger::getInstance()->info("MySQL connection established successfully");

                        self->startHeartbeat(std::chrono::seconds(300));
                    }
                    catch (const std::exception& e) {
                        self->isConnected = false;
                        Logger::getInstance()->error(std::string("MySQL Connection failed: ") + e.what());
                    }
                }
                else {
                    Logger::getInstance()->warning("MsquicMysqlManager instance has been destroyed before connection attempt");
                }
                }, boost::asio::detached);
        }

        void MsquicMysqlManager::startHeartbeat(std::chrono::seconds interval) {
            if (heartbeatRunning) {
                return; // 已经在运行
            }

            heartbeatInterval = interval;
            heartbeatRunning = true;

            Logger::getInstance()->info("Starting MySQL heartbeat, interval: " +
                std::to_string(interval.count()) + " seconds");

            doHeartbeat();
        }

        void MsquicMysqlManager::stopHeartbeat() {
            heartbeatRunning = false;
            heartbeatTimer.cancel();
            Logger::getInstance()->info("MySQL heartbeat stopped");
        }

        void MsquicMysqlManager::doHeartbeat() {
            if (!heartbeatRunning) {
                return;
            }

            // 执行心跳协程
            boost::asio::co_spawn(ioContext,
                [self = shared_from_this()]() -> boost::asio::awaitable<void> {
                    co_await self->executeHeartbeat();
                }, boost::asio::detached);

            // 设置下一次心跳
            heartbeatTimer.expires_after(heartbeatInterval);
            heartbeatTimer.async_wait([this](boost::system::error_code ec) {
                if (!ec && heartbeatRunning) {
                    doHeartbeat();
                }
                });
        }

        boost::asio::awaitable<void> MsquicMysqlManager::executeHeartbeat() {
            try {
                if (!isConnected) {
                    // 尝试重连
                    bool success = co_await checkAndReconnect();
                    if (!success) {
                        Logger::getInstance()->warning("Heartbeat: connection is not available");
                        co_return;
                    }
                }

                // 执行简单查询保持连接活跃
                boost::mysql::results result;
                co_await mysqlConnection->async_execute("SELECT 1 AS heartbeat", result);

                Logger::getInstance()->debug("MySQL heartbeat executed successfully");
            }
            catch (const std::exception& e) {
                Logger::getInstance()->warning("MySQL heartbeat failed: " + std::string(e.what()));
                isConnected = false;

                // 心跳失败后立即尝试重连
                boost::asio::co_spawn(ioContext,
                    [self = shared_from_this()]() -> boost::asio::awaitable<void> {
                        co_await self->checkAndReconnect();
                    }, boost::asio::detached);
            }
        }

        boost::asio::awaitable<bool> MsquicMysqlManager::checkAndReconnect() {
            try {
                if (isConnected) {
                    // 快速检查连接是否仍然有效
                    boost::mysql::results result;
                    co_await mysqlConnection->async_execute("SELECT 1", result);
                    co_return true;
                }

                // 需要重新连接
                boost::mysql::connect_params params;
                params.server_address = boost::mysql::host_and_port(hostIP, static_cast<unsigned short>(port));
                params.username = username;
                params.password = password;
                params.database = database;
                params.ssl = boost::mysql::ssl_mode::disable;

                co_await mysqlConnection->async_connect(params);
                isConnected = true;

                Logger::getInstance()->info("MySQL connection reestablished successfully");
                co_return true;
            }
            catch (const std::exception& e) {
                Logger::getInstance()->error("MySQL reconnection failed: " + std::string(e.what()));
                isConnected = false;
                co_return false;
            }
        }

        std::shared_ptr<boost::mysql::any_connection> MsquicMysqlManager::getConnection() {
            // 返回连接前可以检查状态
            if (!isConnected) {
                Logger::getInstance()->warning("Returning potentially disconnected MySQL connection");
            }
            return mysqlConnection;
        }
    }
}