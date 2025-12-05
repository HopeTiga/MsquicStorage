#pragma once
#pragma once
#include<string>
#include <vector>
#include <thread>
#include <functional>

#include <wtf.h>

#include <boost/asio/awaitable.hpp>

namespace hope {

	namespace quic {

		class MsquicManager;

		class MsquicWebTransportSocket;

		class MsquicWebTransportServer{
		
			friend void MsquicWebTransportLogHandle(wtf_log_level_t level, const char* component, const char* file, int line, const char* message, void* userData);

			friend void MsquicWebTransportSessionHandle(const wtf_session_event_t* event);

			friend wtf_connection_decision_t MsquicWebTransportConnectionValidator(const wtf_connection_request_t* request, void* userData);

		public:

			MsquicWebTransportServer(size_t port = 8088, size_t size = std::thread::hardware_concurrency());

			~MsquicWebTransportServer();

			bool initialize();

			void postTaskAsync(size_t channelIndex, std::function <boost::asio::awaitable<void>(MsquicManager*) > asyncHandle);

			void shutDown();

		private:

			MsquicManager* loadBalanceMsquicManger();

		private:

			size_t port;

			size_t size;

			wtf_server_t* server{ nullptr };

			wtf_context_t* context{ nullptr };

			// 初始化标志
			bool initialized;

			std::vector<MsquicManager*> msquicManagers;

			std::atomic<size_t> loadBalancer{ 0 };


		};

		void MsquicWebTransportLogHandle(wtf_log_level_t level, const char* component,const char* file, int line, const char* message, void* userData);

		void MsquicWebTransportSessionHandle(const wtf_session_event_t* event);

		wtf_connection_decision_t MsquicWebTransportConnectionValidator(const wtf_connection_request_t* request,void* userData);

	}

}
