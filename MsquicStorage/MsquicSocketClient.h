#pragma once

#include <msquic.hpp>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <boost/json.hpp>

namespace hope {
    namespace quic {

        class MsquicSocketClient : public std::enable_shared_from_this<MsquicSocketClient> {

            friend QUIC_STATUS QUIC_API MsquicClientConnectionHandle(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event);
            friend QUIC_STATUS QUIC_API MsquicClientStreamHandle(HQUIC stream, void* context, QUIC_STREAM_EVENT* event);

        public:
            // 构造函数
            MsquicSocketClient(boost::asio::io_context& ioContext);

            // 析构函数
            ~MsquicSocketClient();

            // 初始化客户端
            bool initialize( const std::string& alpn = "quic");

            // 连接到服务器
            bool connect(std::string serverAddress,uint64_t serverPort);

            // 发送数据
            bool writeAsync(unsigned char* data, size_t size);

            // 发送JSON数据
            bool writeJsonAsync(const boost::json::object& json);

            // 断开连接
            void disconnect();

            // 设置消息处理函数
            void setMessageHandle(std::function<void(boost::json::object&)> handle);

            // 设置连接状态处理函数
            void setConnectionHandle(std::function<void(bool)> handle);

            // 是否已连接
            bool isConnected() const;

            // 获取账户ID
            std::string getAccountId() const;

            // 设置账户ID
            void setAccountId(const std::string& accountId);

        private:
            // 创建流
            HQUIC createStream();

            // 处理接收数据
            void handleReceive(QUIC_STREAM_EVENT* event);

            // 尝试解析消息
            void tryParse();

            // 清理资源
            void clear();

        private:
            boost::asio::io_context& ioContext;
            HQUIC connection;
            HQUIC stream;
            HQUIC remoteStream;
            MsQuicRegistration* registration;
            MsQuicConfiguration* configuration;

            std::string serverAddress;
            uint16_t serverPort;
            std::string alpn;

            std::vector<uint8_t> receivedBuffer;
            int64_t payloadLen;

            std::string accountId;
            std::atomic<bool> connected;

            std::function<void(boost::json::object&)> messageHandle;
            std::function<void(bool)> connectionHandle;
        };

        // 声明静态回调函数
        QUIC_STATUS QUIC_API MsquicClientConnectionHandle(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event);
        QUIC_STATUS QUIC_API MsquicClientStreamHandle(HQUIC stream, void* context, QUIC_STREAM_EVENT* event);

    } // namespace quic
} // namespace hope
