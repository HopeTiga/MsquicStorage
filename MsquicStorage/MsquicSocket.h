#pragma once

#include <memory>

#include <msquic.hpp>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

#include "MsquicSocketInterface.h"

namespace hope {

	namespace quic {

		class MsquicManager;
	
		class MsquicSocket :public MsquicSocketInterface, public std::enable_shared_from_this<MsquicSocket>
		{
			friend QUIC_STATUS QUIC_API MsquicSocketHandle(HQUIC stream, void* context, QUIC_STREAM_EVENT* event);
		public:

			MsquicSocket(HQUIC connection, MsquicManager * msquicManager, boost::asio::io_context& ioContext);

			~MsquicSocket();

			void runEventLoop();

			void writeAsync(unsigned char * data,size_t size);

			void setAccountId(const std::string& accountId);

			std::string& getAccountId();

			void setRegistered(bool registered);

			bool getRegistered();

			MsquicManager* getMsquicManager();

			void setRemoteStream(HQUIC remoteStream);

			boost::asio::io_context& getIoCompletionPorts();

			void shutDown();

			void clear();

			SocketType getType();

		private:

			HQUIC createStream();

			void receiveAsync(QUIC_STREAM_EVENT* revice);

			void tryParse();

			boost::asio::awaitable<void> registrationTimeout();

		private:

			MsquicManager* msquicManager;

			HQUIC connection;

			HQUIC stream;

			HQUIC remoteStream;

			boost::asio::io_context& ioContext;

			std::vector<uint8_t>    receivedBuffer;        // 未消费字节

			bool                    headerReady = false;

			short                   msgType = 0;

			int64_t                 payloadLen = 0;

			std::string accountId;

			boost::asio::steady_timer registrationTimer; // 计时器成员

			std::atomic<bool> isRegistered{ false }; // 新增：注册状态标志

			std::atomic<bool> isShutDown{ false };

		};

		QUIC_STATUS QUIC_API MsquicSocketHandle(HQUIC stream,void* context,QUIC_STREAM_EVENT* event);

	}

}



