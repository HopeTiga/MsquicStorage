#include "MsquicSocket.h"
#include "MsquicManager.h"
#include "MsquicData.h"

#include "MsQuicApi.h"

#include "Utils.h"

#include <boost/json.hpp>
#include <boost/asio/co_spawn.hpp>

namespace hope {

    namespace quic {

        MsquicSocket::MsquicSocket(HQUIC connection, MsquicManager* msquicManager, boost::asio::io_context& ioContext) :connection(connection), msquicManager(msquicManager), ioContext(ioContext), registrationTimer(ioContext)
        {
      
        }

        MsquicSocket::~MsquicSocket()
        {

            registrationTimer.cancel();

            clear();

        }

        void MsquicSocket::clear()
        {

            receivedBuffer.clear();

            if (stream) {
                MsQuic->StreamClose(stream);
                stream = nullptr;
            }

            if (remoteStream) {
                MsQuic->StreamClose(remoteStream);
                stream = nullptr;
            }

            if (connection) {
                MsQuic->ConnectionClose(connection);
                connection = nullptr;
            }

        }

        void MsquicSocket::runEventLoop()
        {
           stream = createStream();

           boost::asio::co_spawn(ioContext, [this]()->boost::asio::awaitable<void> {
            
                co_await this->registrationTimeout();

            }, boost::asio::detached);
        }


        void MsquicSocket::writeAsync(unsigned char* data, size_t size)
        {
            QUIC_BUFFER* buffer = new QUIC_BUFFER;

            buffer->Buffer = data;

            buffer->Length = size;

            // 添加更多错误检查
            QUIC_STATUS status = MsQuic->StreamSend(
                stream,
                buffer,
                1,
                QUIC_SEND_FLAG_NONE,
                buffer);

            if (QUIC_FAILED(status)) {

                delete[] buffer->Buffer;

                delete buffer;

                MsQuic->StreamClose(stream);

                return;
            }

            return;
        }

        void MsquicSocket::receiveAsync(QUIC_STREAM_EVENT* event)
        {
            auto* rev = &event->RECEIVE;
            for (uint32_t i = 0; i < rev->BufferCount; ++i) {
                const auto& buf = rev->Buffers[i];
                receivedBuffer.insert(receivedBuffer.end(), buf.Buffer, buf.Buffer + buf.Length);
            }
            tryParse();          // 每次事件都尝试解析
        }

        void MsquicSocket::tryParse()
        {
            constexpr size_t headerSize = sizeof(int64_t);

            while (true) {

                // 1. 检查头部
                if (receivedBuffer.size() < headerSize) return;

                // 2. 预读长度（不删除数据）
                int64_t len = *reinterpret_cast<const int64_t*>(receivedBuffer.data());

                // 3. 检查完整包 (Header + Body)
                // 注意：你的 writeJsonAsync 逻辑是 Header(8字节) + Body(len字节)
                // 所以总长度应该是 headerSize + len
                if (static_cast<int64_t>(receivedBuffer.size()) < headerSize + len) {
                    return; // 数据不够，等待下次
                }

                // 4. 数据完整，开始提取
                // 移除头部
                receivedBuffer.erase(receivedBuffer.begin(), receivedBuffer.begin() + headerSize);

                // 提取 Body
                std::vector<uint8_t> payload(receivedBuffer.begin(), receivedBuffer.begin() + len);

                // 移除 Body
                receivedBuffer.erase(receivedBuffer.begin(), receivedBuffer.begin() + len);

                /* 4. 业务回调（json 在 payload 里） */
                boost::json::object json =
                    boost::json::parse(std::string(payload.begin(), payload.end())).as_object();

                auto msquicData = std::make_shared<MsquicData>(
                    json, this, msquicManager);
                msquicManager->getMsquicLogicSystem()->postTaskAsync(std::move(msquicData));
            }
        }

        boost::asio::awaitable<void> MsquicSocket::registrationTimeout() {

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
                this->clear();

            }

            co_return;
        }


		HQUIC MsquicSocket::createStream()
		{
            HQUIC stream = nullptr;
            QUIC_STATUS status = MsQuic->StreamOpen(
                connection,
                QUIC_STREAM_OPEN_FLAG_NONE,
                MsquicSocketHandle,
                this,
                &stream);

            if (QUIC_FAILED(status)) {
                return nullptr;
            }

            MsQuic->SetCallbackHandler(
                stream,
                MsquicSocketHandle,   // 你的静态流回调
                this);

            status = MsQuic->StreamStart(
                stream,
                QUIC_STREAM_START_FLAG_IMMEDIATE | QUIC_STREAM_START_FLAG_INDICATE_PEER_ACCEPT | QUIC_STREAM_START_FLAG_PRIORITY_WORK);

            if (QUIC_FAILED(status)) {
                MsQuic->StreamClose(stream);
                return nullptr;
            }

  

            return stream;

		}


        void MsquicSocket::setAccountId(const std::string& accountId) { 
        
            this->accountId = accountId;

        }

        std::string& MsquicSocket::getAccountId() { 
        
            return this->accountId;

        }

        void MsquicSocket::setRegistered(bool registered) {
            
            this->isRegistered.store(registered);

            if (isRegistered.load()) {
            
                registrationTimer.cancel();

            }

        }

        bool MsquicSocket::getRegistered() {
            return this->isRegistered ;
        }

        MsquicManager* MsquicSocket::getMsquicManager()
        {
            return msquicManager;
        }

        void MsquicSocket::setRemoteStream(HQUIC remoteStream) {
        
			this->remoteStream = remoteStream;

        }


        // Stream callback
        QUIC_STATUS QUIC_API MsquicSocketHandle(
            HQUIC stream,
            void* context,
            QUIC_STREAM_EVENT* event) {

            MsquicSocket * msquicSocket = static_cast<MsquicSocket*>(context);

            if (msquicSocket == nullptr || event == nullptr) {
                return QUIC_STATUS_INVALID_PARAMETER;
            }

            switch (event->Type) {
            case QUIC_STREAM_EVENT_START_COMPLETE:
            {
                break;
            }
           

            case QUIC_STREAM_EVENT_RECEIVE:
            {

                msquicSocket->receiveAsync(event);

                break;
            }
         

            // Add handler for QUIC_STREAM_EVENT_SEND_COMPLETE (type 2)
            case QUIC_STREAM_EVENT_SEND_COMPLETE:
            {
                if (event->SEND_COMPLETE.ClientContext) {
                    
                    QUIC_BUFFER*  buffer = static_cast<QUIC_BUFFER*>(event->SEND_COMPLETE.ClientContext);

                    if (buffer->Buffer) {
                    
						delete[] buffer->Buffer;

                    }

                    delete buffer;

                }
                break;
            }
    

            // Add handler for QUIC_STREAM_EVENT_PEER_SEND_ABORTED (type 6)
            case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
            {
                break;

            }
   

            case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN: {
                break;
            }
                                           
            case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
            {
                break;
            }
    

            case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
                break;
            }
            case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE:
            {
                break;
            }
        

            default:
            {
                break;
            }
            }

            return QUIC_STATUS_SUCCESS;
        }

	}
}