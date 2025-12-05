#pragma once

#include <unordered_map>
#include <memory>
#include <functional>
#include <utility>

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include "concurrentqueue.h"


namespace hope {

	namespace quic {

		class MsquicData;

	}

	namespace mysql {
	
		class MsquicMysqlManager;

	}

	namespace handle {


		class MsquicLogicSystem : public std::enable_shared_from_this<MsquicLogicSystem>
		{

		public:

			MsquicLogicSystem(boost::asio::io_context& ioContext);

			~MsquicLogicSystem();

			MsquicLogicSystem(const MsquicLogicSystem& logic) = delete;

			void operator=(const MsquicLogicSystem& logic) = delete;

			void postTaskAsync(std::shared_ptr<hope::quic::MsquicData> data);

			void RunEventLoop();

			boost::asio::io_context& getIoCompletePorts();

		private:

			void initHandlers();

			boost::asio::io_context & ioContext;

			std::unordered_map<int, std::pair<bool, std::function<boost::asio::awaitable<void>(std::shared_ptr<hope::quic::MsquicData>,std::shared_ptr<hope::mysql::MsquicMysqlManager>)>>> msquicHandlers;

		};
	}

}

