#include "MsquicSocketClient.h"
#include "MsQuicApi.h"
#include "Utils.h"

namespace hope {
    namespace quic {

        MsquicSocketClient::MsquicSocketClient(boost::asio::io_context& ioContext)
            : ioContext(ioContext)
            , connection(nullptr)
            , stream(nullptr)
            , registration(nullptr)
            , configuration(nullptr)
            , serverPort(0)
            , payloadLen(0)
            , connected(false) {
        }

        MsquicSocketClient::~MsquicSocketClient() {
            clear();
        }

        bool MsquicSocketClient::initialize( const std::string& alpn) {
            if (MsQuic == nullptr) {
                return false;
            }

            this->alpn = alpn;

            // 创建注册
            registration = new MsQuicRegistration("MsquicSocketClient");
            if (!registration->IsValid()) {
                return false;
            }

            // 配置设置
            MsQuicSettings settings;
            settings.SetIdleTimeoutMs(10000);
            settings.SetKeepAlive(5000);
            settings.SetPeerBidiStreamCount(2);

            // 创建ALPN
            MsQuicAlpn alpnBuffer(alpn.c_str());

            // 创建配置
            QUIC_CREDENTIAL_CONFIG credConfig = { 0 };
            credConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
            credConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

            configuration = new MsQuicConfiguration(
                *registration,
                alpnBuffer,
                settings,
                MsQuicCredentialConfig(credConfig)
            );

            return configuration->IsValid();
        }

        bool MsquicSocketClient::connect(std::string serverAddress, uint64_t serverPort) {
            // 如果已有连接，先完全清理
            if (connection != nullptr) {
                LOG_INFO("reclear msquic connection");

                // 立即标记断开，防止新操作
                connected.store(false);

                // 清理流
                if (stream) {
                    MsQuic->StreamShutdown(stream,
                                           QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND |
                                               QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                                           QUIC_STATUS_ABORTED);
                    MsQuic->StreamClose(stream);
                    stream = nullptr;
                }

                if (remoteStream) {
                    MsQuic->StreamShutdown(remoteStream,
                                           QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND |
                                               QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                                           QUIC_STATUS_ABORTED);
                    MsQuic->StreamClose(remoteStream);
                    remoteStream = nullptr;
                }

                // 清理连接
                MsQuic->ConnectionShutdown(
                    connection,
                    QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT,
                    QUIC_STATUS_ABORTED
                    );
                MsQuic->ConnectionClose(connection);
                connection = nullptr;

                // 清空缓冲区
                receivedBuffer.clear();
            }

            this->serverAddress = serverAddress;
            this->serverPort = serverPort;

            // 创建连接
            QUIC_STATUS status = MsQuic->ConnectionOpen(
                *registration,
                MsquicClientConnectionHandle,
                this,
                &connection);

            if (QUIC_FAILED(status)) {
                LOG_ERROR("ConnectionOpen failed: 0x%08X", status);
                return false;
            }

            // 启动连接
            status = MsQuic->ConnectionStart(
                connection,
                *configuration,
                QUIC_ADDRESS_FAMILY_INET,
                serverAddress.c_str(),
                serverPort);

            if (QUIC_FAILED(status)) {
                LOG_ERROR("ConnectionStart failed:0x%08X", status);
                MsQuic->ConnectionClose(connection);
                connection = nullptr;
                return false;
            }

            // 创建流
            stream = createStream();
            if (stream == nullptr) {
                LOG_ERROR("create MsquicStream failed");
                MsQuic->ConnectionClose(connection);
                connection = nullptr;
                return false;
            }

            connected.store(true);
            return true;
        }

        HQUIC MsquicSocketClient::createStream() {
            HQUIC stream = nullptr;
            QUIC_STATUS status = MsQuic->StreamOpen(
                connection,
                QUIC_STREAM_OPEN_FLAG_NONE,
                MsquicClientStreamHandle,
                this,
                &stream);

            if (QUIC_FAILED(status)) {
                return nullptr;
            }

            status = MsQuic->StreamStart(
                stream,
                QUIC_STREAM_START_FLAG_IMMEDIATE);

            if (QUIC_FAILED(status)) {
                MsQuic->StreamClose(stream);
                return nullptr;
            }

            return stream;
        }

        bool MsquicSocketClient::writeAsync(unsigned char* data, size_t size) {
            if (!connected || stream == nullptr) {
                return false;
            }

            QUIC_BUFFER* buffer = new QUIC_BUFFER;
            buffer->Buffer = data;
            buffer->Length = static_cast<uint32_t>(size);

            QUIC_STATUS status = MsQuic->StreamSend(
                stream,
                buffer,
                1,
                QUIC_SEND_FLAG_NONE,
                buffer);

            if (QUIC_FAILED(status)) {
                delete buffer;
                return false;
            }

            return true;
        }

        bool MsquicSocketClient::writeJsonAsync(const boost::json::object& json) {
            // 构建消息格式：length + body
            std::string body = boost::json::serialize(json);
            int64_t bodyLength = static_cast<int64_t>(body.size());
            size_t totalSize = sizeof(int64_t) + bodyLength;

            unsigned char * buffer = new unsigned char[totalSize];

            // 写入length
            *reinterpret_cast<int64_t*>(buffer) = bodyLength;

            // 写入body
            memcpy(buffer + sizeof(int64_t), body.data(), bodyLength);

            return writeAsync(buffer, totalSize);
        }


        void MsquicSocketClient::disconnect() {
            // 防止重复调用
            if (!connected.load() && connection == nullptr) {
                return;
            }

            // 立即标记逻辑断开，阻止新的写入
            connected.store(false);

            // 1. 关闭流
            if (stream) {
                // 先优雅关闭，再关闭
                MsQuic->StreamShutdown(stream,
                                       QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL,
                                       0);
                MsQuic->StreamClose(stream);
                stream = nullptr;
            }

            if (remoteStream) {
                MsQuic->StreamShutdown(remoteStream,
                                       QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL,
                                       0);
                MsQuic->StreamClose(remoteStream);
                remoteStream = nullptr;
            }

            // 2. 优雅关闭连接
            if (connection) {

                // 使用异步关闭，等待 SHUTDOWN_COMPLETE 回调
                MsQuic->ConnectionShutdown(
                    connection,
                    QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                    QUIC_STATUS_SUCCESS
                    );

                connection = nullptr;

            }
        }

        void MsquicSocketClient::setOnDataReceivedHandle(std::function<void(boost::json::object&)> handle) {
            onDataReceivedHandle = std::move(handle);
        }

        void MsquicSocketClient::setOnConnectionHandle(std::function<void(bool)> handle) {
            onConnectionHandle = std::move(handle);
        }

        bool MsquicSocketClient::isConnected() const {
            return connected.load();
        }

        void MsquicSocketClient::handleReceive(QUIC_STREAM_EVENT* event)
        {
            auto* rev = &event->RECEIVE;

            // 1. 如果只有一个缓冲区，按原逻辑处理
            if (rev->BufferCount == 1) {
                const auto& buf = rev->Buffers[0];

                if (buf.Length >= sizeof(int64_t)) {
                    int64_t bodyLen = *reinterpret_cast<const int64_t*>(buf.Buffer);
                    int64_t totalLen = sizeof(int64_t) + bodyLen;

                    if (buf.Length >= totalLen) {
                        // 零拷贝直接解析
                        std::string_view jsonStr(
                            reinterpret_cast<const char*>(buf.Buffer + sizeof(int64_t)),
                            bodyLen
                        );

                        try {
                            auto json = boost::json::parse(jsonStr).as_object();
          
                            if (onDataReceivedHandle) {

                                onDataReceivedHandle(json);

                            }

                        }
                        catch (const std::exception& e) {
                            LOG_ERROR("JSON parse error: %s", e.what());
                        }

                        // 处理剩余数据
                        if (buf.Length > totalLen) {
                            receivedBuffer.assign(
                                buf.Buffer + totalLen,
                                buf.Buffer + buf.Length
                            );
                            tryParse();
                        }
                        return;
                    }
                }
            }
            // 2. 多个缓冲区但数据头完整在第一个缓冲区
            else if (rev->BufferCount > 1) {
                const auto& firstBuf = rev->Buffers[0];

                if (firstBuf.Length >= sizeof(int64_t)) {
                    int64_t bodyLen = *reinterpret_cast<const int64_t*>(firstBuf.Buffer);
                    int64_t totalLen = sizeof(int64_t) + bodyLen;

                    // 计算所有缓冲区的总长度
                    uint64_t totalBytes = 0;
                    for (uint32_t i = 0; i < rev->BufferCount; ++i) {
                        totalBytes += rev->Buffers[i].Length;
                    }

                    // 如果多个缓冲区中包含完整数据包
                    if (totalBytes >= totalLen) {
                        // 从多个缓冲区构建JSON字符串
                        std::string jsonStr;
                        jsonStr.reserve(bodyLen);

                        // 第一个缓冲区：跳过头部
                        uint64_t bytesFromFirst = std::min(
                            static_cast<uint64_t>(firstBuf.Length - sizeof(int64_t)),
                            static_cast<uint64_t>(bodyLen)
                        );
                        jsonStr.append(
                            reinterpret_cast<const char*>(firstBuf.Buffer + sizeof(int64_t)),
                            bytesFromFirst
                        );

                        // 后续缓冲区：添加剩余JSON数据
                        uint64_t jsonBytesCollected = bytesFromFirst;
                        for (uint32_t i = 1; i < rev->BufferCount && jsonBytesCollected < bodyLen; ++i) {
                            const auto& buf = rev->Buffers[i];
                            uint64_t toCopy = std::min(
                                static_cast<uint64_t>(buf.Length),
                                static_cast<uint64_t>(bodyLen - jsonBytesCollected)
                            );
                            jsonStr.append(
                                reinterpret_cast<const char*>(buf.Buffer),
                                toCopy
                            );
                            jsonBytesCollected += toCopy;
                        }

                        try {
                            auto json = boost::json::parse(jsonStr).as_object();
                 
                            if (onDataReceivedHandle) {
                            
                                onDataReceivedHandle(json);

                            }

                            // 处理剩余数据
                            uint64_t consumedBytes = totalLen;
                            receivedBuffer.clear();

                            // 跳过已消费的数据
                            for (uint32_t i = 0; i < rev->BufferCount && consumedBytes > 0; ++i) {
                                const auto& buf = rev->Buffers[i];
                                if (consumedBytes >= buf.Length) {
                                    consumedBytes -= buf.Length;
                                }
                                else {
                                    // 当前缓冲区有剩余数据
                                    uint64_t remaining = buf.Length - consumedBytes;
                                    receivedBuffer.insert(
                                        receivedBuffer.end(),
                                        buf.Buffer + consumedBytes,
                                        buf.Buffer + buf.Length
                                    );
                                    consumedBytes = 0;
                                }
                            }

                            if (!receivedBuffer.empty()) {
                                tryParse();
                            }
                            return;  // 零拷贝处理完毕
                        }
                        catch (const std::exception& e) {
                            LOG_ERROR("JSON parse error: %s", e.what());
                            // 解析失败，回退到缓冲区拷贝方式
                        }
                    }
                }
            }

            // 3. 不能零拷贝解析，回退到原逻辑
            for (uint32_t i = 0; i < rev->BufferCount; ++i) {
                const auto& buf = rev->Buffers[i];
                receivedBuffer.insert(receivedBuffer.end(), buf.Buffer, buf.Buffer + buf.Length);
            }
            tryParse();
        }

        void MsquicSocketClient::tryParse()
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

                if (onDataReceivedHandle) {

                    onDataReceivedHandle(json);

                }
  
            }
        }

        void MsquicSocketClient::clear() {

            onConnectionHandle = nullptr;

            onDataReceivedHandle = nullptr;
            // 先标记断开，防止新操作
            connected.store(false);

            receivedBuffer.clear();

            if (registration) {
                registration->Shutdown(QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
                //delete registration;
                registration = nullptr;
            }

            // 清理流
            if (stream) {
                // 使用中止标志立即关闭
                MsQuic->StreamShutdown(stream,
                                       QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND |
                                           QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                                       QUIC_STATUS_ABORTED);
                MsQuic->StreamClose(stream);
                stream = nullptr;
            }

            if (remoteStream) {
                MsQuic->StreamShutdown(remoteStream,
                                       QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND |
                                           QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                                       QUIC_STATUS_ABORTED);
                MsQuic->StreamClose(remoteStream);
                remoteStream = nullptr;
            }

            // 清理连接（等待一小段时间让异步操作完成）
            if (connection) {
                // 先尝试优雅关闭
                MsQuic->ConnectionShutdown(
                    connection,
                    QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT,
                    QUIC_STATUS_ABORTED
                    );

                MsQuic->ConnectionClose(connection);

                connection = nullptr;
            }

            // 清理配置和注册
            if (configuration) {
                delete configuration;
                configuration = nullptr;
            }

        }

        // 静态连接回调函数
        QUIC_STATUS QUIC_API MsquicClientConnectionHandle(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event) {
            auto* client = static_cast<MsquicSocketClient*>(context);
            if (!client) {
                return QUIC_STATUS_INVALID_PARAMETER;
            }

            switch (event->Type) {
            case QUIC_CONNECTION_EVENT_CONNECTED:
                if (client->onConnectionHandle) {
                    client->onConnectionHandle(true);
                }
                break;

            case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
                LOG_INFO("QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE");

                client->connected.store(false);
                if (client->onConnectionHandle) {
                    client->onConnectionHandle(false);
                }
                break;
            case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            {
                client->remoteStream = event->PEER_STREAM_STARTED.Stream;

                MsQuic->SetCallbackHandler(
                    event->PEER_STREAM_STARTED.Stream,
                    hope::quic::MsquicClientStreamHandle,   // 你的静态流回调
                    client);

                break;
            }

            default:
                break;
            }

            return QUIC_STATUS_SUCCESS;
        }

        // 静态流回调函数
        QUIC_STATUS QUIC_API MsquicClientStreamHandle(HQUIC stream, void* context, QUIC_STREAM_EVENT* event) {
            auto* client = static_cast<MsquicSocketClient*>(context);
            if (!client) {
                return QUIC_STATUS_INVALID_PARAMETER;
            }

            switch (event->Type) {
            case QUIC_STREAM_EVENT_RECEIVE:
                client->handleReceive(event);
                break;

            case QUIC_STREAM_EVENT_SEND_COMPLETE:
                if (event->SEND_COMPLETE.ClientContext) {
                    QUIC_BUFFER* buffer = static_cast<QUIC_BUFFER*>(event->SEND_COMPLETE.ClientContext);
                    delete[] buffer->Buffer;
                    delete buffer;
                }
                break;

            case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
                break;

            default:
                break;
            }

            return QUIC_STATUS_SUCCESS;
        }

    } // namespace quic
} // namespace hope
