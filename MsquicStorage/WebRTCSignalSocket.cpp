#include "WebRTCSignalSocket.h"
#include "MsquicManager.h"
#include "MsquicData.h"

#include "Utils.h"

namespace hope {

    namespace quic {

        WebRTCSignalSocket::WebRTCSignalSocket(boost::asio::io_context& ioContext, hope::quic::MsquicManager * msquicManager)
            : ioContext(ioContext)
            , writerChannel(ioContext, 1)
            , resolver(ioContext)
            , registrationTimer(ioContext)
            , webSocket(ioContext)
            , channelIndex(channelIndex)
            , msquicManager(msquicManager) {
        }

        WebRTCSignalSocket::~WebRTCSignalSocket() {
            clear();
        }

        boost::asio::ip::tcp::socket& WebRTCSignalSocket::getSocket() {

            return webSocket.next_layer();

        }

        void WebRTCSignalSocket::destroy() { // 统一销毁入口
            bool expected = false;
            if (isDeleted.compare_exchange_strong(expected, true)) {
                this->closeSocket();
            }
        }

        boost::asio::io_context& WebRTCSignalSocket::getIoCompletionPorts() {
            return ioContext;
        }

        boost::beast::websocket::stream<boost::asio::ip::tcp::socket>& WebRTCSignalSocket::getWebSocket() {

            return webSocket;

        }

        boost::asio::awaitable<void> WebRTCSignalSocket::handShake() {
            // 假设 webSocket.next_layer() 已经通过 acceptor.async_accept 连接成功

            boost::beast::flat_buffer buffer;
            boost::beast::http::request<boost::beast::http::string_body> req;

            try {
                // 1. 异步读取 HTTP Upgrade 请求
                co_await boost::beast::http::async_read(webSocket.next_layer(), buffer, req, boost::asio::use_awaitable);

                // 打印所有请求头
                for (auto const& field : req) {
                    LOG_INFO("  %s: %s",
                        std::string(field.name_string()).c_str(),
                        std::string(field.value()).c_str());
                }

                // 2. 打印请求目标 (客户端 handshake("/", ...) 中的路径)
                // req.target() 返回 boost::beast::string_view
                const std::string target(req.target());

                // 3. 执行 WebSocket 服务端握手 (async_accept)
                co_await webSocket.async_accept(req, boost::asio::use_awaitable);
                boost::asio::co_spawn(ioContext, [self = shared_from_this()]() -> boost::asio::awaitable<void> {
                    co_await self->registrationTimeout();
                    }, boost::asio::detached);

                buffer.consume(buffer.size());
            }
            catch (const boost::system::system_error& se) {
                LOG_ERROR("WebRTCSignalServer WebSocket handshake failed! ERROR: %s", se.what());
                // ... 错误处理 ...
                destroy();
            }
        }

        // WebRTCSignalSocket.cpp

        boost::asio::awaitable<void> WebRTCSignalSocket::registrationTimeout() {

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
                LOG_WARNING("Rgister Timeout (10s): WebRTCSignalSocket not rigster，close socket.");
                // 调用 stop() 会执行 closeSocket()
                destroy();
            }
        }

        void WebRTCSignalSocket::runEventLoop() {

            boost::asio::co_spawn(ioContext, reviceCoroutine(), [self = shared_from_this()](std::exception_ptr p) {

                if (p) {
                    try {
                        std::rethrow_exception(p);
                    }
                    catch (const std::runtime_error& e) {

                        if (self->isRegistered && self->onDisConnectHandle) {

                            self->onDisConnectHandle(self->accountId);

                        }
                        LOG_ERROR("WebRTCSignalSocket error: %s", e.what());
                    }
                }


                });

            boost::asio::co_spawn(ioContext, writerCoroutine(),boost::asio::detached);

            webSocket.set_option(boost::beast::websocket::stream_base::timeout::suggested(
                boost::beast::role_type::server));

        }

        void WebRTCSignalSocket::clear() {

            if (isStop.exchange(true) == false) {
                LOG_INFO("Stop connect...");
                // 确保所有 IO 操作中断
                closeSocket();
            }
        }

        hope::quic::SocketType WebRTCSignalSocket::getType()
        {
            return hope::quic::SocketType::WebSocket;
        }

        // WebRTCSignalSocket.cpp

        void WebRTCSignalSocket::closeSocket() {

            boost::system::error_code ec;

            webSocket.next_layer().cancel(ec);

            if (ec) {
                LOG_ERROR("WebRTCSignalSocket::closeSocket() can't cancel Socket: %s", ec.message().c_str());
            }

            registrationTimer.cancel();

            // 3. 关闭 WebSocket
            // 发送 WebSocket 关闭帧
            if (webSocket.is_open()) {
                try {
                    // 使用同步 close，因为我们通常在协程外部或清理阶段调用此函数
                    // 协程内部调用 close 通常需要 async_close
                    webSocket.close(boost::beast::websocket::close_code::normal, ec);
                }
                catch (const std::exception& e) {
                    // Beast::close 可能会抛出异常，捕获它
                    LOG_ERROR("WebRTCSignalSocket::closeSocket() close WebSocket failed: %s", e.what());
                }
            }

            // 4. 关闭底层 TCP Socket
            if (webSocket.next_layer().is_open()) {
                webSocket.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                if (ec && ec != boost::asio::error::not_connected) {
                    // 忽略 not_connected 错误
                }
                webSocket.next_layer().close(ec);
                if (ec) {
                    LOG_ERROR("WebRTCSignalSocket::closeSocket() close Tcp Socket failed: %s", ec.message().c_str());
                }
            }

            // 5. 关闭 writerChannel
            writerChannel.close(); // 确保 writerCoroutine 退出等待


            LOG_INFO("WebRTCSignalSocket is close");
        }

        boost::asio::awaitable<void> WebRTCSignalSocket::reviceCoroutine() {

            while (!isStop) {

                boost::beast::flat_buffer buffer;

                co_await webSocket.async_read(buffer, boost::asio::use_awaitable);

                std::string dataStr = boost::beast::buffers_to_string(buffer.data());

                buffer.consume(buffer.size());

                boost::json::object json;

                try {

                    json = boost::json::parse(dataStr).as_object();
               
                }
                catch (std::exception& e) {

                    LOG_ERROR("hope::socket::WebRTCSignalSocket reviceCoroutine  Pase Json Error: %s", e.what());

                    continue;
                }

                std::shared_ptr< hope::quic::MsquicData > data = std::make_shared < hope::quic::MsquicData > (std::move(json), shared_from_this(), msquicManager);

                msquicManager->getMsquicLogicSystem()->postTaskAsync(data);

            }

        }

        boost::asio::awaitable<void> WebRTCSignalSocket::writerCoroutine() {

            for (;;) {

                std::string str;

                while (writerQueues.try_dequeue(str)) {

                    co_await webSocket.async_write(boost::asio::buffer(str), boost::asio::use_awaitable);

                }

                if (!isStop && !isSuppendWrite.exchange(true)) {

                    co_await writerChannel.async_receive(boost::asio::use_awaitable);

                }
                else {

                    std::string str;

                    while (writerQueues.try_dequeue(str)) {

                        co_await webSocket.async_write(boost::asio::buffer(str), boost::asio::use_awaitable);

                    }

                    co_return;
                }

            }

            co_return;

        }

        void WebRTCSignalSocket::writeAsync(unsigned char* data, size_t size)
        {
    
            writeAsync(std::string(reinterpret_cast<const char*>(data), size));
           
            if (data) {
            
                delete[] data;

                data = nullptr;

            }
        }

        void WebRTCSignalSocket::writeAsync(std::string str) {

            writerQueues.enqueue(str);

            if (isSuppendWrite.exchange(false)) {
                writerChannel.async_send(boost::system::error_code(), [](boost::system::error_code ec) {});
            }

        }

        void WebRTCSignalSocket::setOnDisConnectHandle(std::function<void(std::string)> handle)
        {
            this->onDisConnectHandle = handle;
        }


        void WebRTCSignalSocket::setAccountId(const std::string& accountId) { this->accountId = accountId; }

        std::string WebRTCSignalSocket::getAccountId() { return  this->accountId; }

        void WebRTCSignalSocket::setRegistered(bool isRegistered) { this->isRegistered = isRegistered; }
        

    }

}