#include "MsquicLogicSystem.h"
#include "MsquicWebTransportServer.h"
#include "MsquicManager.h"
#include "MsquicWebTransportSocket.h"
#include "MsquicData.h"
#include "MsquicMysqlManagerPools.h"

#include <iostream>
#include <chrono>

#include <boost/uuid/uuid.hpp>            // uuid 类  
#include <boost/uuid/uuid_generators.hpp> // 生成器  
#include <boost/uuid/uuid_io.hpp>   

#include <jwt-cpp/jwt.h>

#include "MsquicHashMap.h"
#include "MsquicHashSet.h"

#include "AsyncTransactionGuard.h"

#include "ConfigManager.h"
#include "GameServers.h"
#include "GameProcesses.h"
#include "Utils.h"

constexpr static char secretKey[] = "913140924@qq.com";

hope::utils::MsquicHashMap<std::string, hope::utils::MsquicHashSet<std::string>> cloudProcessHashMap;

namespace hope {

    namespace handle
    {

        MsquicLogicSystem::MsquicLogicSystem(boost::asio::io_context& ioContext) : ioContext(ioContext)
        {

        }

        void MsquicLogicSystem::RunEventLoop() {
           
            initHandlers();

        }

        boost::asio::io_context& MsquicLogicSystem::getIoCompletePorts()
        {
            return ioContext;
        }

        MsquicLogicSystem::~MsquicLogicSystem() {
			msquicHandlers.clear();
        }

        void MsquicLogicSystem::postTaskAsync(std::shared_ptr<hope::quic::MsquicData> data) {
            
            data->json = makeCleanCopy(data->json);

            int type = data->json["requestType"].as_int64();

            if (this->msquicHandlers.find(type) != this->msquicHandlers.end()) {
                std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>> pairs = this->msquicHandlers[type];

                if (pairs.first) {
                    std::shared_ptr<hope::mysql::MsquicMysqlManager> manager = hope::mysql::MsquicMysqlManagerPools::getInstance()->getTransactionMysqlManager();

                    if (manager) {
                        boost::asio::co_spawn(ioContext, [this, type, pairs, manager, data]() mutable -> boost::asio::awaitable<void> {
                            try {
                                co_await pairs.second(data, manager);
                            }
                            catch (...) {
                                hope::mysql::MsquicMysqlManagerPools::getInstance()->returnTransactionMysqlManager(std::move(manager));
                                throw;
                            }

                            hope::mysql::MsquicMysqlManagerPools::getInstance()->returnTransactionMysqlManager(std::move(manager));
                            },
                            [this, type](std::exception_ptr ptr) {
                                if (ptr) {
                                    try {
                                        std::rethrow_exception(ptr);
                                    }
                                    catch (const std::exception& e) {
                                        LOG_ERROR("MsquicLogicSystem boost::asio::co_spawn Task: %d Exception: %s", type, e.what());
                                    }
                                }
                            });
                    }
                    else {
                        postTaskAsync(data); // 暂不加重试，保持原样
                    }
                }
                else {
                    std::shared_ptr<hope::mysql::MsquicMysqlManager> manager = hope::mysql::MsquicMysqlManagerPools::getInstance()->getMysqlManager();

                    boost::asio::co_spawn(ioContext, [this, type, pairs, manager, data]() -> boost::asio::awaitable<void> {
                        co_await pairs.second(data, manager);
                        },
                        [this, type](std::exception_ptr ptr) {
                            if (ptr) {
                                try {
                                    std::rethrow_exception(ptr);
                                }
                                catch (const std::exception& e) {
                                    LOG_ERROR("MsquicLogicSystem boost::asio::co_spawn Task: %d Exception: %s", type, e.what());
                                }
                            }
                        });
                }
            }
            else {
                LOG_ERROR("Unknown Msquic Request Type: %d", type);
            }
        }

        void MsquicLogicSystem::initHandlers() {
            auto self = shared_from_this();

            std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>, std::string)> forwardHandler = [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager>, std::string requestTypeStr)->boost::asio::awaitable<void> {
                boost::json::object message = data->json;
                auto msquicWebTransportSocket = data->msquicWebTransportSocket;
                int64_t requestTypeValue = message["requestType"].as_int64();

                if (!message.contains("accountId") || !message.contains("targetId")) {
                    LOG_WARNING("Forward Message Missing accountId or targetId.");
                    co_return;
                }

                std::string accountId = message["accountId"].as_string().c_str();
                std::string targetId = message["targetId"].as_string().c_str();
                hope::quic::MsquicWebTransportSocket * targetSocket = nullptr;

                // 1. 查找目标连接 (使用哈希锁)
                {
                    auto it = data->msquicManager->msquicSocketMap.find(targetId);
                    if (it != data->msquicManager->msquicSocketMap.end()) {
                        targetSocket = it->second;
                    }
                }

                // 2. 处理目标未找到 (404)
                if (!targetSocket) {
                    tbb::concurrent_lru_cache<std::string, int>::handle handles = data->msquicManager->localRouteCache[targetId];
                    auto self = data->msquicManager;

                    if (handles.value() == -1) {
                        int mapChannelIndex = data->msquicManager->hasher(targetId) % data->msquicManager->hashSize;

                        data->msquicManager->msquicWebTransportServer->postTaskAsync(mapChannelIndex, [=](hope::quic::MsquicManager * manager)->boost::asio::awaitable<void> {
                            if (manager->actorSocketMappingIndex.find(targetId) != manager->actorSocketMappingIndex.end()) {
                                int targetChannelIndex = manager->actorSocketMappingIndex[targetId];

                                self->msquicWebTransportServer->postTaskAsync(targetChannelIndex, [=](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {
                                    if (manager->msquicSocketMap.find(targetId) != manager->msquicSocketMap.end()) {
                                        if (tbb::concurrent_lru_cache<std::string, int>::handle handles = self->localRouteCache[targetId]) {
                                            handles.value() = manager->channelIndex;
                                        }

                                        boost::json::object forwardMessage = message;
                                        forwardMessage["state"] = 200;
                                        forwardMessage["message"] = "MsquicWebTransportServer forward";

                                        // 构建二进制消息
                                        auto [buffer, size] = buildMessage(forwardMessage);
                                        manager->msquicSocketMap[targetId]->writeAsync(buffer, size);

                                        LOG_INFO("Request forward: %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                        co_return;
                                    }
                                    else {
                                        boost::json::object response;
                                        response["requestType"] = requestTypeValue;
                                        response["state"] = 404;
                                        response["message"] = "targetId is not register";

                                        // 构建二进制消息
                                        auto [buffer, size] = buildMessage(response);
                                        msquicWebTransportSocket->writeAsync(buffer, size);

                                        LOG_WARNING("Request forward Not Found (404): %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                        co_return;
                                    }
                                    });
                            }
                            else {
                                boost::json::object response;
                                response["requestType"] = requestTypeValue;
                                response["state"] = 404;
                                response["message"] = "targetId is not register";

                                // 构建二进制消息
                                auto [buffer, size] = buildMessage(response);
                                msquicWebTransportSocket->writeAsync(buffer, size);

                                LOG_WARNING("Request forward Not Found (404): %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                co_return;
                            }
                            });
                    }
                    else {
                        data->msquicManager->msquicWebTransportServer->postTaskAsync(handles.value(), [=](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {
                            if (manager->msquicSocketMap.find(targetId) != manager->msquicSocketMap.end()) {
                                if (tbb::concurrent_lru_cache<std::string, int>::handle handles = self->localRouteCache[targetId]) {
                                    handles.value() = manager->channelIndex;
                                }

                                boost::json::object forwardMessage = message;
                                forwardMessage["state"] = 200;
                                forwardMessage["message"] = "MsquicWebTransportServer forward";

                                // 构建二进制消息
                                auto [buffer, size] = buildMessage(forwardMessage);
                                manager->msquicSocketMap[targetId]->writeAsync(buffer, size);

                                LOG_INFO("Request forward: %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                co_return;
                            }
                            else {
                                int mapChannelIndex = data->msquicManager->hasher(targetId) % data->msquicManager->hashSize;

                                data->msquicManager->msquicWebTransportServer->postTaskAsync(mapChannelIndex, [=](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {
                                    if (manager->actorSocketMappingIndex.find(targetId) != manager->actorSocketMappingIndex.end()) {
                                        int targetChannelIndex = manager->actorSocketMappingIndex[targetId];

                                        self->msquicWebTransportServer->postTaskAsync(targetChannelIndex, [=](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {
                                            if (manager->msquicSocketMap.find(targetId) != manager->msquicSocketMap.end()) {
                                                if (tbb::concurrent_lru_cache<std::string, int>::handle handles = self->localRouteCache[targetId]) {
                                                    handles.value() = manager->channelIndex;
                                                }

                                                boost::json::object forwardMessage = message;
                                                forwardMessage["state"] = 200;
                                                forwardMessage["message"] = "MsquicWebTransportServer forward";

                                                // 构建二进制消息
                                                auto [buffer, size] = buildMessage(forwardMessage);
                                                manager->msquicSocketMap[targetId]->writeAsync(buffer, size);

                                                LOG_INFO("Request forward: %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                                co_return;
                                            }
                                            else {
                                                boost::json::object response;
                                                response["requestType"] = requestTypeValue;
                                                response["state"] = 404;
                                                response["message"] = "targetId is not register";

                                                // 构建二进制消息
                                                auto [buffer, size] = buildMessage(response);
                                                msquicWebTransportSocket->writeAsync(buffer, size);

                                                LOG_WARNING("Request forward Not Found (404): %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                                co_return;
                                            }
                                            });
                                    }
                                    else {
                                        boost::json::object response;
                                        response["requestType"] = requestTypeValue;
                                        response["state"] = 404;
                                        response["message"] = "targetId is not register";

                                        // 构建二进制消息
                                        auto [buffer, size] = buildMessage(response);
                                        msquicWebTransportSocket->writeAsync(buffer, size);

                                        LOG_WARNING("Request forward Not Found (404): %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                        co_return;
                                    }
                                    });
                            }
                            });
                    }
                    co_return;
                }

                // 3. 转发消息
                boost::json::object forwardMessage = message;
                forwardMessage["state"] = 200;
                forwardMessage["message"] = "MsquicWebTransportServer forward";

                // 构建二进制消息
                auto [buffer, size] = buildMessage(forwardMessage);
                targetSocket->writeAsync(buffer, size);

                LOG_INFO("Request forward: %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                };

            // REGISTER handler
            msquicHandlers[0] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                boost::json::object& message = data->json;
                auto msquicWebTransportSocket = data->msquicWebTransportSocket;
                boost::json::object response;
                response["requestType"] = static_cast<int64_t>(0);

                if (!message.contains("authorization")) {
                    LOG_WARNING("REGISTER Message Missing Authorization.");
                    response["state"] = 500;
                    response["message"] = "REGISTER Message Missing Authorization.";

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_return;
                }

                std::string authorization = message["authorization"].as_string().c_str();
                std::string accountId = "";

                try {
                    jwt::decoded_jwt<jwt::traits::kazuho_picojson> jwtAuthorization = jwt::decode(authorization);
                    jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson> verifier = jwt::verify().allow_algorithm(jwt::algorithm::hs256{ secretKey });
                    verifier.verify(jwtAuthorization);

                    if (!jwtAuthorization.has_payload_claim("accountId")) {
                        response["state"] = 500;
                        response["message"] = "REGISTER Authorization Missing accountId.";

                        auto [buffer, size] = buildMessage(response);
                        msquicWebTransportSocket->writeAsync(buffer, size);
                        co_return;
                    }

                    accountId = jwtAuthorization.get_payload_claim("accountId").as_string();
                }
                catch (const std::exception& e) {
                    LOG_ERROR("Jwt-CPP Decode ERROR: %s", e.what());
                    response["state"] = 500;
                    response["message"] = "Jwt-CPP Decode ERROR: " + std::string(e.what());

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_return;
                }

                msquicWebTransportSocket->setAccountId(accountId);
                msquicWebTransportSocket->setRegistered(true);
                data->msquicManager->msquicSocketMap[accountId] = msquicWebTransportSocket;

                response["state"] = 200;
                response["message"] = "register successful";

                auto [buffer, size] = buildMessage(response);
                msquicWebTransportSocket->writeAsync(buffer, size);

                int mapChannelIndex = data->msquicManager->hasher(accountId) % data->msquicManager->hashSize;

                data->msquicManager->msquicWebTransportServer->postTaskAsync(mapChannelIndex, [self = data->msquicManager, accountId, mapChannelIndex](hope::quic::MsquicManager * manager)->boost::asio::awaitable<void> {
                    manager->actorSocketMappingIndex[accountId] = self->channelIndex;
                    co_return;
                    });

                LOG_INFO("User Register Successful : %s (channelIndex: %d)", accountId.c_str(), data->msquicManager->channelIndex);
                });

            // REQUEST handler
            msquicHandlers[1] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self, forwardHandler](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                co_await forwardHandler(std::move(data), mysqlManager, "REQUEST");
                });

            // RESTART handler
            msquicHandlers[2] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self, forwardHandler](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                co_await forwardHandler(std::move(data), mysqlManager, "RESTART");
                });

            // STOPREMOTE handler
            msquicHandlers[3] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self, forwardHandler](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                co_await forwardHandler(std::move(data), mysqlManager, "STOPREMOTE");
                });

            // CLOSE handler
            msquicHandlers[4] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                std::string accountId = data->msquicWebTransportSocket->getAccountId();

                if (!accountId.empty()) {
                    data->msquicManager->removeConnection(accountId);
                }
                delete data->msquicWebTransportSocket;

                LOG_INFO("Receive User %s CLOSE Request，MsquicWebTransportSocket is Stop", accountId.c_str());

				co_return;
                });

            // CLOUD_GAME_SERVERS_REGISTER handler
            msquicHandlers[5] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> manager)->boost::asio::awaitable<void> {
                boost::json::object response;
                response["requestType"] = static_cast<int64_t>(5);

                auto msquicWebTransportSocket = data->msquicWebTransportSocket;
                auto json = data->json;
                auto signalManager = data->msquicManager;
                auto conn = manager->getConnection();

                std::string serverIP = msquicWebTransportSocket->getRemoteAddress();

                boost::mysql::results selectResult;
                boost::mysql::statement stmt = co_await conn->async_prepare_statement("select * from game_servers where ip_address = ?", boost::asio::use_awaitable);
                co_await conn->async_execute(stmt.bind(serverIP), selectResult);

                if (!selectResult.rows().empty()) {
                    response["state"] = 500;
                    response["message"] = "The Cloud Server ip already be Register";

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_return;
                }

                if (!json.contains("maxProcess") || !json.contains("name") || !json.contains("hostname") ||
                    !json.contains("location") || !json.contains("region")) {
                    response["state"] = 400;
                    response["message"] = "Missing required field";

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_return;
                }

                boost::uuids::random_generator gen;
                std::string serverId = boost::uuids::to_string(gen());
                int max_process = json["maxProcess"].as_int64();
                std::string name = json["name"].as_string().c_str();
                std::string hostname = json["hostname"].as_string().c_str();
                std::string location = json["location"].as_string().c_str();
                std::string region = json["region"].as_string().c_str();
                std::string tags = json.contains("tags") ? boost::json::serialize(json["tags"]) : "{}";
                std::string specifications = json.contains("specifications") ? boost::json::serialize(json["specifications"]) : "{}";

                boost::mysql::statement insertStmt = co_await conn->async_prepare_statement(
                    R"(INSERT INTO game_servers (server_id, ip_address, name, hostname, location, specifications, max_processes, 
        current_processes, status, region, tags, last_heartbeat, created_at, updated_at, del_flag) VALUES (?, ?, ?, ?, ?, ?, ?, 0, 'online', ?, ?, NOW(), NOW(), NOW(), 0))",
                    boost::asio::use_awaitable
                );

                boost::mysql::results insertResult;
                co_await conn->async_execute(
                    insertStmt.bind(
                        serverId, serverIP, name, hostname,
                        location, specifications, max_process,
                        region, tags
                    ), insertResult,
                    boost::asio::use_awaitable
                );

                response["state"] = 200;
                response["message"] = "Register Cloud Game Process Successful";
                response["serverId"] = serverId;
                response["maxProcess"] = max_process;

                auto [buffer, size] = buildMessage(response);
                msquicWebTransportSocket->writeAsync(buffer, size);

                signalManager->msquicSocketMap[serverId] = msquicWebTransportSocket;
                msquicWebTransportSocket->setRegistered(true);
                msquicWebTransportSocket->setAccountId(serverId);

                LOG_INFO("CloudGame is Register Successful: ID=%s, IP=%s, Name=%s", serverId.c_str(), serverIP.c_str(), name.c_str());
                co_return;
                });

            // CLOUD_GAME_SERVERS_LOGIN handler
            msquicHandlers[6] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> manager)->boost::asio::awaitable<void> {
                boost::json::object json = data->json;
                hope::quic::MsquicWebTransportSocket * msquicWebTransportSocket = data->msquicWebTransportSocket;
                hope::quic::MsquicManager * signalManager = data->msquicManager;
                std::shared_ptr<boost::mysql::any_connection> connection = manager->getConnection();

                boost::json::object response;
                response["requestType"] = static_cast<int64_t>(6);

                std::string serverIP = msquicWebTransportSocket->getRemoteAddress();

                boost::mysql::results selectResult;
                boost::mysql::statement stmt = co_await connection->async_prepare_statement("select * from game_servers where ip_address = ?", boost::asio::use_awaitable);
                co_await connection->async_execute(stmt.bind(serverIP), selectResult);

                if (selectResult.rows().empty()) {
                    response["state"] = 501;
                    response["message"] = "The Cloud Server ip not Register";

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_return;
                }

                hope::entity::GameServers gameServer(selectResult.rows()[0]);

                if (gameServer.status == "online") {
                    response["state"] = 500;
                    response["message"] = "The Cloud Server Already Login";

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_return;
                }

                boost::mysql::results updateResult;
                boost::mysql::statement updateStmt = co_await connection->async_prepare_statement(
                    "UPDATE game_servers SET status = 'online', last_heartbeat = NOW(), updated_at = NOW() WHERE server_id = ?",
                    boost::asio::use_awaitable
                );

                co_await connection->async_execute(updateStmt.bind(gameServer.server_id), updateResult);

                if (updateResult.affected_rows() == 0) {
                    response["state"] = 502;
                    response["message"] = "Cloud Game Server Login Failed";

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_return;
                }

                msquicWebTransportSocket->setRegistered(true);
                msquicWebTransportSocket->setCloudServer(true);
                response["state"] = 200;
                response["message"] = "Cloud Game Server Login Successful";
                response["serverId"] = gameServer.server_id;
                msquicWebTransportSocket->setAccountId(gameServer.server_id);
                signalManager->msquicSocketMap[gameServer.server_id] = msquicWebTransportSocket;
                signalManager->msquicCloudServerSocketMap[gameServer.server_id] = msquicWebTransportSocket;

                signalManager->msquicWebTransportServer->postTaskAsync(signalManager->hasher(gameServer.server_id) % signalManager->hashSize, [self = signalManager, serverId = gameServer.server_id](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {
                    manager->actorSocketMappingIndex[serverId] = self->channelIndex;
                    co_return;
                    });

                auto [buffer, size] = buildMessage(response);
                msquicWebTransportSocket->writeAsync(buffer, size);
                co_return;
                });

            // CLOUD_PROCESS_LOGIN handler
            msquicHandlers[7] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(true, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                LOG_INFO("Process CLOUD_PROCESS_LOGIN Request");
                boost::json::object response;
                response["requestType"] = 7;

                auto msquicWebTransportSocket = data->msquicWebTransportSocket;
                auto json = data->json;
                auto signalManager = data->msquicManager;
                auto conn = mysqlManager->getConnection();

                std::string serverId = json["serverId"].as_string().c_str();
                std::string processName = json["processName"].as_string().c_str();
                std::string gameType = json["gameType"].as_string().c_str();
                std::string gameVersion = json["gameVersion"].as_string().c_str();
                std::string serverIP = msquicWebTransportSocket->getRemoteAddress();

                auto sendError = [&](int code, const std::string& msg) {
                    response["state"] = code;
                    response["message"] = msg;
                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    };

                auto registerSocket = [&](const std::string& processId, const std::string& gameType) {
                    msquicWebTransportSocket->setRegistered(true);
                    msquicWebTransportSocket->setAccountId(processId);
                    msquicWebTransportSocket->setCloudProcess(true);
                    msquicWebTransportSocket->setGameType(gameType);
                    signalManager->msquicSocketMap[processId] = msquicWebTransportSocket;
                    signalManager->msquicCloudProcessSocketMap[processId] = msquicWebTransportSocket;

                    if (cloudProcessHashMap.find(gameType) == cloudProcessHashMap.end()) {
                        cloudProcessHashMap.insert(gameType, std::move(hope::utils::MsquicHashSet<std::string>()));
                    }

                    cloudProcessHashMap[gameType].insert(processId);

                    int mapChannelIndex = data->msquicManager->hasher(processId) % data->msquicManager->hashSize;
                    data->msquicManager->msquicWebTransportServer->postTaskAsync(mapChannelIndex, [self = data->msquicManager, processId, mapChannelIndex](hope::quic::MsquicManager * manager)->boost::asio::awaitable<void> {
                        manager->actorSocketMappingIndex[processId] = self->channelIndex;
                        co_return;
                        });
                    };

                auto updateCurrentProcesses = [&](int64_t cur) -> boost::asio::awaitable<bool> {
                    boost::mysql::statement stmt = co_await conn->async_prepare_statement(
                        "UPDATE game_servers SET current_processes = ? WHERE server_id = ?", boost::asio::use_awaitable);
                    boost::mysql::results r;
                    co_await conn->async_execute(stmt.bind(cur, serverId), r, boost::asio::use_awaitable);
                    co_return r.affected_rows() == 1;
                    };

                boost::mysql::results dummy;

                hope::mysql::AsyncTransactionGuard asyncTransactionGuard = co_await hope::mysql::AsyncTransactionGuard::create(conn);

                try {
                    boost::mysql::statement stmt = co_await conn->async_prepare_statement(
                        "SELECT * FROM game_servers WHERE server_id = ? AND ip_address = ?", boost::asio::use_awaitable);
                    boost::mysql::results result;
                    co_await conn->async_execute(stmt.bind(serverId, serverIP), result, boost::asio::use_awaitable);

                    if (result.rows().empty()) {
                        sendError(404, "server not register");
                        co_await asyncTransactionGuard.asyncRollback();
                        co_return;
                    }
                    hope::entity::GameServers gameServer(result.rows()[0]);

                    stmt = conn->prepare_statement("SELECT * FROM game_processes WHERE server_id = ?");
                    co_await conn->async_execute(stmt.bind(serverId), result, boost::asio::use_awaitable);

                    std::vector<hope::entity::GameProcesses> allProcesses;
                    for (auto&& row : result.rows()) {
                        allProcesses.emplace_back(row);
                    }

                    std::string assignedProcessId;

                    if (allProcesses.empty() && gameServer.max_processes > 0) {
                        boost::uuids::random_generator gen;
                        assignedProcessId = boost::uuids::to_string(gen());

                        stmt = co_await conn->async_prepare_statement(
                            "INSERT INTO game_processes "
                            "(process_id, server_id, process_name, game_type, game_version, "
                            "is_idle, startup_parameters, health_status, "
                            "last_health_check, last_heartbeat, started_at, "
                            "created_at, updated_at, del_flag, is_login) "
                            "VALUES (?, ?, ?, ?, ?, 1, '', 'healthy', NULL, NULL, NOW(), "
                            "        NOW(), NOW(), 0, 1)", boost::asio::use_awaitable);

                        boost::mysql::results ins;
                        co_await conn->async_execute(
                            stmt.bind(assignedProcessId, serverId, processName, gameType, gameVersion),
                            ins, boost::asio::use_awaitable);

                        if (ins.affected_rows() != 1) {
                            throw std::runtime_error("GameProcess Insert Error");
                        }

                        if (!co_await updateCurrentProcesses(gameServer.current_processes + 1)) {
                            throw std::runtime_error("GameServers Update CurrentProcess Error");
                        }

                        LOG_INFO("Create process on first login: %s", assignedProcessId.c_str());
                    }
                    else {
                        std::vector<hope::entity::GameProcesses> idle;
                        for (const auto& p : allProcesses) {
                            if (p.game_type == gameType && p.is_login == 0 && p.is_idle == 1 &&
                                p.del_flag == 0 && p.health_status == "healthy") {
                                idle.push_back(p);
                            }
                        }

                        if (!idle.empty()) {
                            assignedProcessId = idle[0].process_id;
                            stmt = co_await conn->async_prepare_statement(
                                "UPDATE game_processes SET is_login = 1, is_idle = 1, last_heartbeat = NOW() "
                                "WHERE process_id = ?", boost::asio::use_awaitable);
                            co_await conn->async_execute(stmt.bind(assignedProcessId), result, boost::asio::use_awaitable);
                            LOG_INFO("ReUse The ProcessID: %s", assignedProcessId.c_str());
                        }
                        else if (static_cast<int64_t>(allProcesses.size()) < gameServer.max_processes) {
                            boost::uuids::random_generator gen;
                            assignedProcessId = boost::uuids::to_string(gen());

                            stmt = co_await conn->async_prepare_statement(
                                "INSERT INTO game_processes "
                                "(process_id, server_id, process_name, game_type, game_version, "
                                "is_idle, startup_parameters, health_status, "
                                "last_health_check, last_heartbeat, started_at, "
                                "created_at, updated_at, del_flag, is_login) "
                                "VALUES (?, ?, ?, ?, ?, 1, '', 'healthy', NULL, NULL, NOW(), "
                                "        NOW(), NOW(), 0, 1)", boost::asio::use_awaitable);

                            boost::mysql::results ins;
                            co_await conn->async_execute(
                                stmt.bind(assignedProcessId, serverId, processName, gameType, gameVersion),
                                ins, boost::asio::use_awaitable);

                            if (ins.affected_rows() != 1) {
                                throw std::runtime_error("GameProcess Insert Error");
                            }

                            if (!co_await updateCurrentProcesses(gameServer.current_processes + 1)) {
                                throw std::runtime_error("GameServers Update CurrentProcess Error");
                            }

                            LOG_INFO("New Create Process（Already Login）: %s", assignedProcessId.c_str());
                        }
                        else {
                            sendError(507, "The Server is Max Process,Can't Create More Process");
                            co_await asyncTransactionGuard.asyncRollback();
                            co_return;
                        }
                    }

                    registerSocket(assignedProcessId, gameType);
                    response["state"] = 200;
                    response["message"] = "Process Allocate Successful";
                    response["processId"] = assignedProcessId;
                    response["processName"] = processName;
                    response["gameType"] = gameType;

                    auto [buffer, size] = buildMessage(response);
                    msquicWebTransportSocket->writeAsync(buffer, size);
                    co_await asyncTransactionGuard.commit();
                }
                catch (const std::exception& e) {
                    asyncTransactionGuard.rollback();
                    sendError(500, e.what());
                }

                co_return;
                });

            // CLOUD_PROCESS_LOGOUT handler
            msquicHandlers[9] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> manager)->boost::asio::awaitable<void> {
                std::shared_ptr<boost::mysql::any_connection> connection = manager->getConnection();
                std::string accountId = data->json["accountId"].as_string().c_str();

                boost::mysql::results updateResult;
                boost::mysql::statement updateStmt = co_await connection->async_prepare_statement(
                    "UPDATE game_processes SET is_login = 0 ,is_idle = 1 ,last_heartbeat = NOW(), updated_at = NOW() WHERE process_id = ?",
                    boost::asio::use_awaitable
                );

                co_await connection->async_execute(updateStmt.bind(accountId), updateResult);

                if (updateResult.affected_rows() == 0) {
                    LOG_WARNING("Cloud Process Logout Failed: %s", accountId.c_str());
                    co_return;
                }

                LOG_INFO("Cloud Process Logout Successful: %s", accountId.c_str());
                co_return;
                });

            msquicHandlers[10] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> manager)->boost::asio::awaitable<void> {
                co_return;
                });

            // CLOUD_GAME_START handler
            msquicHandlers[11] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> manager)->boost::asio::awaitable<void> {
                hope::quic::MsquicWebTransportSocket* msquicWebTransportSocket = data->msquicWebTransportSocket;
                hope::quic::MsquicManager* signalManager = data->msquicManager;
                std::shared_ptr<boost::mysql::any_connection> connection = manager->getConnection();
                boost::json::object json = data->json;

                boost::json::object response;
                response["requestType"] = json["requestType"].as_int64();
                std::string processId = json["accountId"].as_string().c_str();

                // 从空闲集合中移除
                std::string gameType = msquicWebTransportSocket->getGameType();
                if (!gameType.empty() && cloudProcessHashMap.find(gameType) != cloudProcessHashMap.end()) {
                    cloudProcessHashMap[gameType].erase(processId);
                }

                LOG_INFO("Cloud Game Start Successful: %s", processId.c_str());
                co_return;
                });

            // CLOUD_GAME_STOP handler
            msquicHandlers[12] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> manager)->boost::asio::awaitable<void> {
                hope::quic::MsquicWebTransportSocket* msquicWebTransportSocket = data->msquicWebTransportSocket;
                hope::quic::MsquicManager* signalManager = data->msquicManager;
                std::shared_ptr<boost::mysql::any_connection> connection = manager->getConnection();
                boost::json::object json = data->json;

                boost::json::object response;
                response["requestType"] = json["requestType"].as_int64();
                std::string processId = json["accountId"].as_string().c_str();
                std::string gameType = json["gameType"].as_string().c_str();

                // 添加到空闲集合
                if (cloudProcessHashMap.find(gameType) == cloudProcessHashMap.end()) {
                    cloudProcessHashMap.insert(gameType, std::move(hope::utils::MsquicHashSet<std::string>()));
                }
                cloudProcessHashMap[gameType].insert(processId);

                LOG_INFO("Cloud Game Stop Successful: %s", processId.c_str());
                co_return;
                });

            // USER_GET_GAMES_PROCESS_ID handler
            msquicHandlers[13] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> manager)->boost::asio::awaitable<void> {
                hope::quic::MsquicWebTransportSocket * msquicWebTransportSocket = data->msquicWebTransportSocket;
                hope::quic::MsquicManager * signalManager = data->msquicManager;
                std::shared_ptr<boost::mysql::any_connection> connection = manager->getConnection();
                boost::json::object& json = data->json;

                boost::json::object response;
                response["requestType"] = json["requestType"].as_int64();
                std::string gameType = json["gameType"].as_string().c_str();

                if (cloudProcessHashMap.find(gameType) != cloudProcessHashMap.end()) {
                    std::optional<std::string> value = cloudProcessHashMap[gameType].pop();

                    if (value.has_value()) {
                        std::string processId = value.value();
                        json["targetId"] = processId;
                        json["requestType"] = static_cast<int64_t>(1);

                        self->postTaskAsync(std::move(data));
                        co_return;
                    }
                }

                response["state"] = 500;
                response["message"] = "No Available Game ProcessID";

                auto [buffer, size] = buildMessage(response);
                msquicWebTransportSocket->writeAsync(buffer, size);
                });
        }

    }

}