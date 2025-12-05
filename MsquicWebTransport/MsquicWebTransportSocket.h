#pragma once
#include <wtf.h>
#include <boost/asio.hpp>

namespace hope {
	namespace quic {

		class MsquicManager;

		class MsquicWebTransportSocket
		{
			friend void MsquicWebTransportStreamHandle(const wtf_stream_event_t* event);;
		public:

			MsquicWebTransportSocket(wtf_session_t * session,MsquicManager* msquicManager,boost::asio::io_context & ioContext);

			virtual ~MsquicWebTransportSocket();

			void runEventLoop();

			void writeAsync(unsigned char* data, size_t size);

			void setAccountId(const std::string& accountId);

			std::string& getAccountId();

			void setRegistered(bool registered);

			bool getRegistered();

			MsquicManager* getMsquicManager();

			boost::asio::awaitable<void> registrationTimeout();

			void addRemoteStream(wtf_stream_t * stream);

			std::string getRemoteAddress();

			void setCloudProcess(bool cloudProcess);

			bool getCloudProcess();

			void setCloudServer(bool cloudServer);

			bool getCloudServer();

			void setGameType(std::string gameType);

			std::string getGameType();


		private:

			void clear();

			bool createStream();

		private:
			
			wtf_session_t* session;
			
			MsquicManager* msquicManager;

			boost::asio::io_context& ioContext;

			std::string accountId;

			boost::asio::steady_timer registrationTimer; // 计时器成员

			std::atomic<bool> isRegistered{ false }; // 新增：注册状态标志

			wtf_stream_t* stream;

			wtf_stream_t* remoteStream;

			std::atomic<bool> cloudProcess{ false };

			std::atomic<bool> cloudServer{ false };

			std::string gameType;

		};

		void MsquicWebTransportStreamHandle(const wtf_stream_event_t* event);

	} // namespace quic
} // namespace hope

