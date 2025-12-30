#pragma once
#include <memory>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/json.hpp>

#include "MsquicSocketInterface.h"

#include "concurrentqueue.h"


namespace hope {

	namespace quic {
	
		class MsquicManager;

		class WebRTCSignalSocket : public hope::quic::MsquicSocketInterface,public std::enable_shared_from_this<WebRTCSignalSocket>
		{
		public:

			WebRTCSignalSocket(boost::asio::io_context& ioContext, hope::quic::MsquicManager* msquicServer);

			~WebRTCSignalSocket();

			boost::asio::ip::tcp::socket& getSocket();

			boost::beast::websocket::stream<boost::asio::ip::tcp::socket>& getWebSocket();

			boost::asio::awaitable<void> handShake();

			boost::asio::io_context& getIoCompletionPorts();

			void runEventLoop();

			void clear();

			virtual void writeAsync(unsigned char* data, size_t size);

			void writeAsync(std::string str);

			void setAccountId(const std::string& accountId);

			std::string getAccountId();

			void setRegistered(bool isRegistered);

			void destroy();

			hope::quic::SocketType getType();

			std::string getGameType();

		public:

			void setOnDisConnectHandle(std::function<void(std::string)> handle);

		private:

			void closeSocket();

			boost::asio::awaitable<void> registrationTimeout();

			boost::asio::awaitable<void> reviceCoroutine();

			boost::asio::awaitable<void> writerCoroutine();

			void setTcpKeepAlive(boost::asio::ip::tcp::socket& socket,int idle = 0, int intvl = 3, int probes = 3);

		private:

			hope::quic::MsquicManager* msquicManager;

			boost::asio::io_context& ioContext;

			boost::beast::websocket::stream<boost::asio::ip::tcp::socket> webSocket;

			boost::asio::ip::tcp::resolver resolver;

			moodycamel::ConcurrentQueue<std::string> writerQueues{ 1 };

			boost::asio::experimental::concurrent_channel<void(boost::system::error_code)> writerChannel;

			std::atomic<bool> isStop{ false };

			std::string accountId;

			std::atomic<bool> isSuppendWrite{ false };

			boost::asio::steady_timer registrationTimer; // 计时器成员

			std::atomic<bool> isRegistered{ false }; // 新增：注册状态标志

			int channelIndex;

			std::atomic<bool> isHandleDisConnect{ false };

			std::atomic<bool> isDeleted{ false };

		private:

			std::function<void(std::string)> onDisConnectHandle;
		};

	}

}
