#pragma once
#define TBB_PREVIEW_CONCURRENT_LRU_CACHE 1
#include <msquic.hpp>
#include <memory>
#include <string>

#include <tbb/concurrent_lru_cache.h>

#include "MsquicLogicSystem.h"
#include "MsquicHashMap.h"

namespace hope {

	namespace quic {

		class MsquicServer;

		class MsquicSocketInterface;

		class MsquicManager : public std::enable_shared_from_this<MsquicManager>
		{
			friend class hope::handle::MsquicLogicSystem;
		public:

			MsquicManager(size_t channelIndex, boost::asio::io_context& ioContext, MsquicServer* msquicServer);

			~MsquicManager();

			std::shared_ptr<hope::handle::MsquicLogicSystem> getMsquicLogicSystem();

			void removeConnection(std::string accountId);

		private:

			boost::asio::io_context& ioContext;

			MsquicServer* msquicServer;

			size_t channelIndex;

			std::shared_ptr<hope::handle::MsquicLogicSystem> logicSystem;

			hope::utils::MsquicHashMap<std::string,std::shared_ptr<MsquicSocketInterface>> msquicSocketInterfaceMap;

			size_t hashSize = std::thread::hardware_concurrency();

			hope::utils::MsquicHashMap<std::string, int> actorSocketMappingIndex;

			tbb::concurrent_lru_cache<std::string, int> localRouteCache;

			std::hash<std::string> hasher;

		};

	}

} // namespace hope
