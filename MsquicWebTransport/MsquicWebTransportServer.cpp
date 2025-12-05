#include "MsquicWebTransportServer.h"
#include "MsquicManager.h"
#include "MsquicWebTransportSocket.h"

#include "AsioProactors.h"

#include "ConfigManager.h"
#include "Logger.h"
#include "Utils.h"

namespace hope {

	namespace quic {

		MsquicWebTransportServer::MsquicWebTransportServer(size_t port, size_t size):port(port),size(size), msquicManagers(size)
		{
            for (int i = 0; i < size; i++) {
                std::pair<size_t, boost::asio::io_context&> pairs =
                    hope::iocp::AsioProactors::getInstance()->getIoCompletePorts();
                msquicManagers[i] = new MsquicManager(pairs.first, pairs.second, this);
            }
		}

		MsquicWebTransportServer::~MsquicWebTransportServer()
		{
			shutDown();
		}

		bool MsquicWebTransportServer::initialize()
		{

            if (initialized) return false;
            // 创建上下文
            wtf_context_config_t contextConfig;

            memset(&contextConfig, 0, sizeof(contextConfig));

            contextConfig.log_level =  WTF_LOG_LEVEL_INFO;

            contextConfig.log_callback = MsquicWebTransportLogHandle;

            wtf_result_t status = wtf_context_create(&contextConfig, &context);

            if (status != WTF_SUCCESS) {

                LOG_ERROR("Create Msquic Context failed: %s\n", wtf_result_to_string(status));

                return false;
            }

            std::string certFileStr = ConfigManager::Instance().GetString("MquicWebTransportServer.certificateFile");

            std::string privateKeyStr = ConfigManager::Instance().GetString("MquicWebTransportServer.privateKeyFile");

            // 配置证书
            wtf_certificate_config_t certConfig;
            memset(&certConfig, 0, sizeof(certConfig));
            certConfig.cert_type = WTF_CERT_TYPE_FILE;
            certConfig.cert_data.file.cert_path = certFileStr.c_str();
            certConfig.cert_data.file.key_path = privateKeyStr.c_str();

            // 配置服务器
            wtf_server_config_t serverConfig;
            memset(&serverConfig, 0, sizeof(serverConfig));
            serverConfig.port = port;
            serverConfig.cert_config = &certConfig;
            serverConfig.session_callback = MsquicWebTransportSessionHandle;
            serverConfig.connection_validator = MsquicWebTransportConnectionValidator;
            serverConfig.max_sessions_per_connection = 32;
            serverConfig.max_streams_per_session = 256;
            serverConfig.idle_timeout_ms = 10000;
            serverConfig.handshake_timeout_ms = 10000;
            serverConfig.user_context = this;


            // 创建服务器
            status = wtf_server_create(context, &serverConfig, &server);
            if (status != WTF_SUCCESS) {
                printf("Create MsquicServer failed: %s\n", wtf_result_to_string(status));
                wtf_context_destroy(context);
                context = nullptr;
                return false;
            }

            status = wtf_server_start(server);

            if (status != WTF_SUCCESS) {
                printf("Start MsquicWebTransportServer failed: %s\n", wtf_result_to_string(status));
                return false;
            }

            initialized = true;

            LOG_INFO("MsquicWebTransportServer Accept Port: %d  initialize SUCCESS", port);

            return true;
		}

		void MsquicWebTransportServer::postTaskAsync(size_t channelIndex, std::function<boost::asio::awaitable<void>(MsquicManager*)> asyncHandle)
		{
            shutDown();
		}

		void MsquicWebTransportServer::shutDown()
		{
		}

		MsquicManager* MsquicWebTransportServer::loadBalanceMsquicManger()
		{
            size_t index = loadBalancer.fetch_add(1) % size;

            return msquicManagers[index];
		}

        void MsquicWebTransportLogHandle(wtf_log_level_t level, const char* component, const char* file, int line, const char* message, void* userData)
        {
            // 转换日志级别
            if (level == WTF_LOG_LEVEL_ERROR || level == WTF_LOG_LEVEL_CRITICAL) {
                LOG_ERROR("[WebTransport] [%s] %s:%d - %s", component, file, line, message);
            }
            else if (level == WTF_LOG_LEVEL_WARN) {
                LOG_WARNING("[WebTransport] [%s] %s:%d - %s", component, file, line, message);
            }
            else if (level == WTF_LOG_LEVEL_INFO) {
                LOG_INFO("[WebTransport] [%s] %s:%d - %s", component, file, line, message);
            }
            else if (level == WTF_LOG_LEVEL_DEBUG) {
                LOG_DEBUG("[WebTransport] [%s] %s:%d - %s", component, file, line, message);
            }
            else if (level == WTF_LOG_LEVEL_TRACE) {
                LOG_DEBUG("[WebTransport] [%s] %s:%d - %s", component, file, line, message);
            }
            // WTF_LOG_LEVEL_NONE 不记录
        }

        void MsquicWebTransportSessionHandle(const wtf_session_event_t* event)
        {

            switch (event->type) {

            case WTF_SESSION_EVENT_CONNECTED: {

                MsquicWebTransportServer* msquicWebTransportServer = static_cast<MsquicWebTransportServer*> (event->user_context);

                MsquicManager * msquicManager = msquicWebTransportServer->loadBalanceMsquicManger();
             
				MsquicWebTransportSocket* msuqicWebTransportSocket = new MsquicWebTransportSocket(event->session, msquicManager ,msquicManager->getMsquicLogicSystem()->getIoCompletePorts());

				msuqicWebTransportSocket->runEventLoop();

                wtf_session_set_context(event->session, msuqicWebTransportSocket);

                break;
            }

            case WTF_SESSION_EVENT_STREAM_OPENED: {

                wtf_session_t* session = event->session;

                // 2. 取 session 的上下文（就是你之前设的 MsquicWebTransportSocket*）
                MsquicWebTransportSocket* msuqicWebTransportSocket =
                    static_cast<MsquicWebTransportSocket*>(wtf_session_get_context(session));

                msuqicWebTransportSocket->addRemoteStream(event->stream_opened.stream);

                wtf_stream_set_callback(event->stream_opened.stream, MsquicWebTransportStreamHandle);

                wtf_stream_set_context(event->stream_opened.stream, msuqicWebTransportSocket);
                
                break;
            }

            case WTF_SESSION_EVENT_DATAGRAM_RECEIVED: {
               
            }

            case WTF_SESSION_EVENT_DISCONNECTED: {
              
                MsquicWebTransportSocket* msuqicWebTransportSocket = static_cast<MsquicWebTransportSocket*>(wtf_session_get_context(event->session));

                if (msuqicWebTransportSocket) {

                    msuqicWebTransportSocket->getMsquicManager()->removeConnection(msuqicWebTransportSocket->getAccountId());

                    delete msuqicWebTransportSocket;

                    msuqicWebTransportSocket = nullptr;

                }

                break;
            }

            default:
                break;
            }
        }

        wtf_connection_decision_t MsquicWebTransportConnectionValidator(const wtf_connection_request_t* request, void* userData)
        {
            LOG_INFO("[Connection] Receive from %s%s Connection Request \n",
                request->authority ? request->authority : "unknown",
                request->path ? request->path : "/");
            return WTF_CONNECTION_ACCEPT;
        }

    }

}
