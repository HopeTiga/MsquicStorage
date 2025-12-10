#include "MsquicManager.h"

#include <boost/asio.hpp>

#include "MsquicServer.h"
#include "MsquicSocket.h"

#include "Utils.h"

namespace hope {

	namespace quic {
	
		MsquicManager::MsquicManager(size_t channelIndex, boost::asio::io_context& ioContext,MsquicServer * msquicServer) 
			: channelIndex(channelIndex)
			, ioContext(ioContext)
			, msquicServer(msquicServer)
			, localRouteCache([](std::string) -> int {
			return -1;
				}, 100)
		{
			logicSystem = std::make_shared<hope::handle::MsquicLogicSystem>(ioContext);

			logicSystem->RunEventLoop();
		}

		MsquicManager::~MsquicManager()
		{
			actorSocketMappingIndex.clear();

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

            msquicServer->postTaskAsync(mapChannelIndex, [self = shared_from_this(), accountId](std::shared_ptr<MsquicManager> manager) -> boost::asio::awaitable<void> {

                manager->actorSocketMappingIndex.erase(accountId);

                co_return;

                });

		}

	}

}