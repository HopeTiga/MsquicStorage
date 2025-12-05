#include "MsquicWebTransportSocket.h"

#include <boost/json.hpp>

#include "MsquicManager.h"
#include "MsquicData.h"

#include "Utils.h"

namespace hope {
	namespace quic {
		MsquicWebTransportSocket::MsquicWebTransportSocket(wtf_session_t* session, MsquicManager* msquicManager, boost::asio::io_context& ioContext)
			: session(session), msquicManager(msquicManager), ioContext(ioContext),stream(nullptr),remoteStream(nullptr),
			registrationTimer(ioContext)
		{
		}

		MsquicWebTransportSocket::~MsquicWebTransportSocket()
		{
            clear();
		}
		void MsquicWebTransportSocket::runEventLoop()
		{
            if (createStream()) {
            
				LOG_INFO("MsquicWebTransportSocket", "Create bidirectional stream SUCCESS");

            }
            boost::asio::co_spawn(ioContext, [this]()->boost::asio::awaitable<void> {

                co_await this->registrationTimeout();

                }, boost::asio::detached);

		}
		void MsquicWebTransportSocket::writeAsync(unsigned char* data, size_t size)
		{
			wtf_buffer_t* buffer = new wtf_buffer_t();

			buffer->data = data;

            buffer->length = size;

            wtf_result_t result = wtf_stream_send(stream, buffer, 1,false);
            if (result != WTF_SUCCESS) {
                LOG_ERROR("Failed to send stream data: %s", wtf_result_to_string(result));
                return ;
            }
		}

        void MsquicWebTransportSocket::clear()
        {
            // 1. 取消所有异步操作
            registrationTimer.cancel();

            // 2. 关闭并清理本地流
            if (stream != nullptr) {

                wtf_stream_close(stream);

                stream = nullptr;
            }

            if (remoteStream != nullptr) {

                wtf_stream_close(remoteStream);

                remoteStream = nullptr;
            }

            if (session) {
            
                uint32_t error_code = 0;

                const char* reason = "Socket cleared";

                if (!isRegistered.load()) {

                    error_code = 1;  // 注册超时或其他注册失败

                    reason = "Registration failed or timeout";
                }

                wtf_session_close(session, error_code, reason);

                session = nullptr;


            }

            // 5. 清理管理器引用
            msquicManager = nullptr;

            // 6. 重置状态
            isRegistered.store(false);

            accountId.clear();
        }

        bool MsquicWebTransportSocket::createStream()
        {

            if (wtf_session_create_stream(session, wtf_stream_type_t::WTF_STREAM_BIDIRECTIONAL, &stream) != WTF_SUCCESS) {
            
                false;

            }

            return true;
        }


        boost::asio::awaitable<void> MsquicWebTransportSocket::registrationTimeout() {

            using namespace std::chrono_literals;

            // 1. 设置 10 秒超时
            registrationTimer.expires_after(10s);

            // 2. 异步等待计时器或被取消
            boost::system::error_code ec;

            co_await registrationTimer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));

            // 3. 检查是否超时
            if (ec == boost::asio::error::operation_aborted) {
                // 计时器被取消 (说明在 10s 内完成了注册)
                co_return;
            }

            // 4. 超时发生，且尚未注册，则关闭连接
            if (!isRegistered.load()) {

				LOG_WARNING("MsquicWebTransportSocket Registration timeout, closing socket.");

                this->clear();

            }

            co_return;
        }

        void MsquicWebTransportSocket::addRemoteStream(wtf_stream_t* stream)
        {
            this->remoteStream = stream;
        }

        void MsquicWebTransportSocket::setAccountId(const std::string& accountId) {

            this->accountId = accountId;

        }

        std::string& MsquicWebTransportSocket::getAccountId() {

            return this->accountId;

        }

        void MsquicWebTransportSocket::setRegistered(bool registered) {

            this->isRegistered.store(registered);

            if (isRegistered.load()) {

                registrationTimer.cancel();

            }

        }

        bool MsquicWebTransportSocket::getRegistered() {
            return this->isRegistered;
        }

        MsquicManager* MsquicWebTransportSocket::getMsquicManager()
        {
            return msquicManager;
        }

        std::string MsquicWebTransportSocket::getRemoteAddress() {
            char ip_str[INET_ADDRSTRLEN];
            size_t buffer_size = sizeof(ip_str);

            // 注意：这个函数期望的是sockaddr结构体，不是字符串缓冲区
            // 你需要先获取原始地址，然后转换
            struct sockaddr_storage addr;
            size_t addr_len = sizeof(addr);

            wtf_result_t result = wtf_session_get_peer_address(session, &addr, &addr_len);
            if (result == WTF_SUCCESS) {
                if (addr.ss_family == AF_INET) {
                    struct sockaddr_in* addr_in = (struct sockaddr_in*)&addr;
                    inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
                    return ip_str;
                }
                else if (addr.ss_family == AF_INET6) {
                    struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)&addr;
                    inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, sizeof(ip_str));
                    return ip_str;
                }
            }

            return "";
        }

        void MsquicWebTransportSocket::setCloudProcess(bool cloudGame) { this->cloudProcess.store(cloudGame); };

        bool MsquicWebTransportSocket::getCloudProcess() { return this->cloudProcess.load(); };

        void MsquicWebTransportSocket::setCloudServer(bool cloudServer) { this->cloudServer.store(cloudServer); };

        bool MsquicWebTransportSocket::getCloudServer() { return this->cloudServer.load(); }

        void MsquicWebTransportSocket::setGameType(std::string gameType)
        {
            this->gameType = gameType;
        }
        std::string MsquicWebTransportSocket::getGameType()
        {
            return this->gameType;
        }

        void MsquicWebTransportStreamHandle(const wtf_stream_event_t* event)
        {

            MsquicWebTransportSocket * msquicWebTransportSocket = static_cast<MsquicWebTransportSocket*>(event->user_context);

            switch (event->type) {

            case WTF_STREAM_EVENT_DATA_RECEIVED: {

                char * data = reinterpret_cast<char *>(event->data_received.buffers->data);

				boost::json::object jsonObj = boost::json::parse(std::string(data, event->data_received.buffers->length)).as_object();

				std::shared_ptr<MsquicData> msquicData = std::make_shared<MsquicData>(makeCleanCopy(jsonObj), msquicWebTransportSocket, msquicWebTransportSocket->getMsquicManager());

                msquicWebTransportSocket->getMsquicManager()->getMsquicLogicSystem()->postTaskAsync(std::move(msquicData));

                break;
            }

            case WTF_STREAM_EVENT_CLOSED: {

                break;
            }

            case WTF_STREAM_EVENT_SEND_COMPLETE: {

                if (event->send_complete.buffers) {
                
                    if (event->send_complete.buffers->data) {
                    
                        delete[] event->send_complete.buffers->data;

                    }

                }
            
                break;
            }

            default:
                break;
            }
        }
}
}
