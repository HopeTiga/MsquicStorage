#include "MsquicManager.h"

#include <boost/asio.hpp>

#include "MsquicWebTransportServer.h"
#include "MsquicWebTransportSocket.h"
#include "MsquicData.h"

#include "Utils.h"

namespace hope {

	namespace quic {
	
		MsquicManager::MsquicManager(size_t channelIndex, boost::asio::io_context& ioContext, MsquicWebTransportServer* msquicWebTransportServer)
			: channelIndex(channelIndex)
			, ioContext(ioContext)
			, msquicWebTransportServer(msquicWebTransportServer)
			, localRouteCache([](std::string) -> int {
			return -1;
				}, 100)
		{
			logicSystem = std::make_shared<hope::handle::MsquicLogicSystem>(ioContext);

			logicSystem->RunEventLoop();
		}

		MsquicManager::~MsquicManager()
		{
			for (std::pair<std::string, MsquicWebTransportSocket*> pairs : msquicCloudServerSocketMap) {

				if (pairs.second) {

					boost::json::object json;

					json["requestType"] = 7;

					json["accountId"] = pairs.second->getAccountId();

					std::shared_ptr<MsquicData> data = std::make_shared<MsquicData>(std::move(json), nullptr, this);

					this->logicSystem->postTaskAsync(std::move(data));

					delete pairs.second;

				}

			}

			msquicCloudServerSocketMap.clear();

			for (std::pair<std::string, MsquicWebTransportSocket * > pairs : msquicCloudProcessSocketMap) {

				if (pairs.second) {

					boost::json::object json;

					json["requestType"] = 9;

					json["accountId"] = pairs.second->getAccountId();

					std::shared_ptr<MsquicData> data = std::make_shared<MsquicData>(std::move(json), nullptr, this);

					this->logicSystem->postTaskAsync(std::move(data));

					delete pairs.second;

				}

			}

			msquicCloudProcessSocketMap.clear();

			msquicSocketMap.clear();

		}

		std::shared_ptr<hope::handle::MsquicLogicSystem> MsquicManager::getMsquicLogicSystem()
		{
			return logicSystem;
		}

		void MsquicManager::removeConnection(std::string accountId)
		{

            LOG_INFO("Remove MsquicSocket: %s", accountId.c_str());

			auto it = msquicSocketMap.find(accountId);

			if (it == msquicSocketMap.end()) {

				LOG_WARNING("Connection already removed: %s", accountId.c_str());

				return;
			}

			msquicSocketMap.erase(it);

            int mapChannelIndex = hasher(accountId) % hashSize;

            LOG_INFO("Start Async Post Task: %d", mapChannelIndex);

			msquicWebTransportServer->postTaskAsync(mapChannelIndex, [self = this, accountId](hope::quic::MsquicManager * manager) -> boost::asio::awaitable<void> {

                manager->actorSocketMappingIndex.erase(accountId);

                co_return;

                });

		}

	}

}