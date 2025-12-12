#pragma once
#include<string>

#include <msquic.hpp>
#include <vector>
#include <thread>
#include <functional>

#include <boost/asio/awaitable.hpp>

namespace hope {

	namespace quic {

		class MsquicManager;

		class MsquicServer
		{
			friend QUIC_STATUS QUIC_API MsquicAcceptHandle(HQUIC listener, void* context, QUIC_LISTENER_EVENT* event);

			friend QUIC_STATUS QUIC_API MsquicConnectionHandle(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event);

		public:

			MsquicServer(size_t port = 8088,std::string alpn = "quic",size_t size = std::thread::hardware_concurrency() );

			~MsquicServer();

			bool initialize();

			bool RunEventLoop();

			MsQuicRegistration* getRegistration();

			MsQuicConfiguration* getConfiguration();

			void postTaskAsync(size_t channelIndex, std::function <boost::asio::awaitable<void>(std::shared_ptr<MsquicManager>) > asyncHandle);

			void shutDown();

		private:

			std::shared_ptr<MsquicManager> loadBalanceMsquicManger();

		private:

			size_t port;

			std::string alpn;

			size_t size;

			MsQuicRegistration* registration;

			// MsQuic 配置
			MsQuicConfiguration* configuration;

			// MsQuic 监听器
			HQUIC listener;

			// 初始化标志
			bool initialized;

			std::vector<std::shared_ptr<MsquicManager>> msquicManagers;

			std::atomic<size_t> loadBalancer{ 0 };

			QUIC_EXECUTION** executions;

			HANDLE* ioCompletionPorts;

			QUIC_EXECUTION_CONFIG* configs;

			std::vector<std::thread> iocpThreads;

			std::atomic<bool> iocpRunEvent{ false };

		};


		QUIC_STATUS QUIC_API MsquicAcceptHandle(HQUIC listener, void* context, QUIC_LISTENER_EVENT* event);

		QUIC_STATUS QUIC_API MsquicConnectionHandle(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event);

	}

}
