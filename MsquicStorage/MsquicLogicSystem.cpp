#include "MsquicLogicSystem.h"
#include "MsquicServer.h"
#include "MsquicManager.h"
#include "MsquicSocket.h"
#include "MsquicData.h"

#include "MsquicMysqlManagerPools.h"

#include <iostream>
#include <chrono>

#include <boost/uuid/uuid.hpp>            // uuid 类  
#include <boost/uuid/uuid_generators.hpp> // 生成器  
#include <boost/uuid/uuid_io.hpp>   

#include "MsquicHashMap.h"
#include "MsquicHashSet.h"

#include "AsyncTransactionGuard.h"

#include "ConfigManager.h"
#include "Utils.h"


namespace hope {

    namespace handle
    {

		MsquicLogicSystem::MsquicLogicSystem(boost::asio::io_context& ioContext) :ioContext(ioContext)
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

        }

        void MsquicLogicSystem::postTaskAsync(std::shared_ptr<hope::quic::MsquicData> data) {

            data->json = makeCleanCopy(data->json);

            int type = data->json["requestType"].as_int64();

            if (this->msquicHandlers.find(type) != this->msquicHandlers.end()) {

                std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr <hope::quic::MsquicData> , std::shared_ptr<hope::mysql::MsquicMysqlManager>)>> pairs = this->msquicHandlers[type];

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
                auto msquicSocket = data->msquicSocket;
                int64_t requestTypeValue = message["requestType"].as_int64();

                if (!message.contains("accountId") || !message.contains("targetId")) {
                    LOG_WARNING("Forward Message Missing accountId or targetId.");
                    co_return;
                }

                std::string accountId = message["accountId"].as_string().c_str();
                std::string targetId = message["targetId"].as_string().c_str();
                hope::quic::MsquicSocket* targetSocket = nullptr;

                // 1. 查找目标连接
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

                        data->msquicManager->msquicServer->postTaskAsync(mapChannelIndex, [=](hope::quic::MsquicManager * manager) ->boost::asio::awaitable<void> {

                            if (manager->actorSocketMappingIndex.find(targetId) != manager->actorSocketMappingIndex.end()) {
                                int targetChannelIndex = manager->actorSocketMappingIndex[targetId];

                                self->msquicServer->postTaskAsync(targetChannelIndex, [=](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {

                                    if (manager->msquicSocketMap.find(targetId) != manager->msquicSocketMap.end()) {
                                        if (tbb::concurrent_lru_cache<std::string, int>::handle handles = self->localRouteCache[targetId]) {
                                            handles.value() = manager->channelIndex;
                                        }

                                        boost::json::object forwardMessage = message;
                                        forwardMessage["state"] = 200;
                                        forwardMessage["message"] = "MsquicStorage forward";

                                        // 修改这里：构建二进制消息
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

                                        // 修改这里：构建二进制消息
                                        auto [buffer, size] = buildMessage(response);
                                        msquicSocket->writeAsync(buffer, size);

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

                                // 修改这里：构建二进制消息
                                auto [buffer, size] = buildMessage(response);
                                msquicSocket->writeAsync(buffer, size);

                                LOG_WARNING("Request forward Not Found (404): %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                co_return;
                            }
                            });
                    }
                    else {
                        data->msquicManager->msquicServer->postTaskAsync(handles.value(), [=](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {

                            if (manager->msquicSocketMap.find(targetId) != manager->msquicSocketMap.end()) {
                                if (tbb::concurrent_lru_cache<std::string, int>::handle handles = self->localRouteCache[targetId]) {
                                    handles.value() = manager->channelIndex;
                                }

                                boost::json::object forwardMessage = message;
                                forwardMessage["state"] = 200;
                                forwardMessage["message"] = "MsquicStorage forward";

                                // 修改这里：构建二进制消息
                                auto [buffer, size] = buildMessage(forwardMessage);
                                manager->msquicSocketMap[targetId]->writeAsync(buffer, size);

                                LOG_INFO("Request forward: %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                                co_return;
                            }
                            else {
                                int mapChannelIndex = data->msquicManager->hasher(targetId) % data->msquicManager->hashSize;

                                data->msquicManager->msquicServer->postTaskAsync(mapChannelIndex, [=](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {

                                    if (manager->actorSocketMappingIndex.find(targetId) != manager->actorSocketMappingIndex.end()) {
                                        int targetChannelIndex = manager->actorSocketMappingIndex[targetId];

                                        self->msquicServer->postTaskAsync(targetChannelIndex, [=](hope::quic::MsquicManager* manager) ->boost::asio::awaitable<void> {

                                            if (manager->msquicSocketMap.find(targetId) != manager->msquicSocketMap.end()) {
                                                if (tbb::concurrent_lru_cache<std::string, int>::handle handles = self->localRouteCache[targetId]) {
                                                    handles.value() = manager->channelIndex;
                                                }

                                                boost::json::object forwardMessage = message;
                                                forwardMessage["state"] = 200;
                                                forwardMessage["message"] = "MsquicStorage forward";

                                                // 修改这里：构建二进制消息
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

                                                // 修改这里：构建二进制消息
                                                auto [buffer, size] = buildMessage(response);
                                                msquicSocket->writeAsync(buffer, size);

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

                                        // 修改这里：构建二进制消息
                                        auto [buffer, size] = buildMessage(response);
                                        msquicSocket->writeAsync(buffer, size);

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
                forwardMessage["message"] = "MsquicStorage forward";

                // 修改这里：构建二进制消息
                auto [buffer, size] = buildMessage(forwardMessage);
                targetSocket->writeAsync(buffer, size);

                LOG_INFO("Request forward: %s -> %s (Request Type: %s)", accountId.c_str(), targetId.c_str(), requestTypeStr);
                };

            msquicHandlers[0] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {

                boost::json::object& message = data->json;
                auto msquicSocket = data->msquicSocket;
                boost::json::object response;
                response["requestType"] = 0;

                if (!message.contains("accountId")) {
                    LOG_WARNING("REGISTER Message Missing accountId.");
                    response["state"] = 500;
                    response["message"] = "REGISTER Message Missing accountId.";

                    // 修改这里：构建二进制消息
                    auto [buffer, size] = buildMessage(response);
                    msquicSocket->writeAsync(buffer, size);

                    co_return;
                }

                std::string accountId = message["accountId"].as_string().c_str();
                msquicSocket->setAccountId(accountId);
                msquicSocket->setRegistered(true);
                data->msquicManager->msquicSocketMap[accountId] = msquicSocket;

                response["state"] = 200;
                response["message"] = "register successful";

                // 修改这里：构建二进制消息
                auto [buffer, size] = buildMessage(response);
                msquicSocket->writeAsync(buffer, size);

                int mapChannelIndex = data->msquicManager->hasher(accountId) % data->msquicManager->hashSize;

                data->msquicManager->msquicServer->postTaskAsync(mapChannelIndex, [self = data->msquicManager, accountId, mapChannelIndex](hope::quic::MsquicManager* manager)->boost::asio::awaitable<void> {
                    manager->actorSocketMappingIndex[accountId] = self->channelIndex;
                    co_return;
                    });

                LOG_INFO("User Register Successful : %s (channelIndex: %d)", accountId.c_str(), data->msquicManager->channelIndex);
                });

            // 其他 handler 保持原来的转发逻辑，因为它们调用 forwardHandler
            msquicHandlers[1] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self, forwardHandler](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                co_await forwardHandler(std::move(data), mysqlManager, "REQUEST");
                });

            msquicHandlers[2] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self, forwardHandler](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                co_await forwardHandler(std::move(data), mysqlManager, "RESTART");
                });

            msquicHandlers[3] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self, forwardHandler](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                co_await forwardHandler(std::move(data), mysqlManager, "STOPREMOTE");
                });

            msquicHandlers[4] = std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>, std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>(false, [self](std::shared_ptr<hope::quic::MsquicData> data, std::shared_ptr<hope::mysql::MsquicMysqlManager> mysqlManager)->boost::asio::awaitable<void> {
                std::string accountId = data->msquicSocket->getAccountId();
                if (!accountId.empty()) {
                    data->msquicManager->removeConnection(accountId);
                }
                co_return;
                });
        }

    }

}



