#define QUIC_API_ENABLE_PREVIEW_FEATURES 1
#define QUIC_VERSION_2          0x6b3343cfU     // Second official version (host byte order)
#define QUIC_VERSION_1          0x00000001U     // First official version (host byte order)

#include "MsquicServer.h"
#include "MsquicManager.h"
#include "MsquicSocket.h"
#include "MsQuicApi.h"

#include "AsioProactors.h"
#include "ConfigManager.h"

#include "Logger.h"
#include "Utils.h"

namespace hope {

    namespace quic {


        // Constants definition
        const uint32_t supportedVersions[] = { QUIC_VERSION_1, QUIC_VERSION_2 };

        const MsQuicVersionSettings versionSettings(supportedVersions, 2);

        MsquicServer::MsquicServer(size_t port, std::string alpn, size_t size)
            : port(port), alpn(alpn), size(size), msquicManagers(size) {

            for (int i = 0; i < size; i++) {
                std::pair<size_t, boost::asio::io_context&> pairs =
                    hope::iocp::AsioProactors::getInstance()->getIoCompletePorts();
                msquicManagers[i] = new MsquicManager(pairs.first, pairs.second, this);
            }
        }

        MsquicServer::~MsquicServer()
        {
            shutDown();
        }

        void MsquicServer::shutDown()
        {
            if (listener != nullptr) {
                MsQuic->ListenerStop(listener);
                MsQuic->ListenerClose(listener);
                listener = nullptr;
            }
            if (configuration != nullptr) {
                delete configuration;
                configuration = nullptr;
            }
            if (registration != nullptr) {
                registration = nullptr;
            }
            initialized = false;

            for (int i = 0; i < msquicManagers.size(); i++) {
            
                if (msquicManagers[i]) {

                    delete msquicManagers[i];

                }

            }

            msquicManagers.clear();
        }

        bool MsquicServer::initialize()
        {
            // Check if MsQuicApi is valid
            if (MsQuic == nullptr) {
                LOG_ERROR("initialize failed: MsQuic global pointer is null");
                return false;
            }

            QUIC_STATUS initStatus = MsQuic->GetInitStatus();
            if (QUIC_FAILED(initStatus)) {
                LOG_ERROR("initialize failed: MsQuic init failed");
                return false;
            }

            // Create registration
            registration = new MsQuicRegistration("MsquicStorage");
            if (!registration->IsValid()) {
                LOG_ERROR("initialize failed: registration invalid");
                delete registration;
                registration = nullptr;
                return false;
            }

            // Configure server settings
            MsQuicSettings settings;
            settings.SetIdleTimeoutMs(10000);
            settings.SetKeepAlive(5000);
            settings.SetPeerBidiStreamCount(2);

            // 正确获取字符串
            std::string certFileStr = ConfigManager::Instance().GetString("MsquicStorage.certificateFile");

            std::string privateKeyStr = ConfigManager::Instance().GetString("MsquicStorage.privateKeyFile");

            QUIC_CERTIFICATE_FILE certFile = {};

            certFile.CertificateFile = certFileStr.c_str();

            certFile.PrivateKeyFile = privateKeyStr.c_str();                // PFX 不需要单独的私钥文件

            QUIC_CREDENTIAL_CONFIG credConfig = {};

            credConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;

            credConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;

            credConfig.CertificateFile = &certFile;

            // Create ALPN buffer
            MsQuicAlpn alpnBuffer(alpn.c_str());

            // Create configuration
            configuration = new MsQuicConfiguration(
                *registration,
                alpnBuffer,
                settings,
                MsQuicCredentialConfig(credConfig));

            if (!configuration->IsValid()) {
                QUIC_STATUS configStatus = configuration->GetInitStatus();
                LOG_ERROR("configuration create failed, status=0x%08X", configStatus);
                delete configuration;
                configuration = nullptr;
                return false;
            }

            // Set version
            configuration->SetVersionSettings(versionSettings);
            configuration->SetVersionNegotiationExtEnabled();

            // Create listener
            QUIC_STATUS status = MsQuic->ListenerOpen(
                *registration,
                MsquicAcceptHandle,
                this,
                &listener);

            if (QUIC_FAILED(status)) {
                LOG_ERROR("initialize failed: ListenerOpen status=0x%08X", status);
                return false;
            }

            // Start listening
            QUIC_ADDR addr = { 0 };
            QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_INET);
            QuicAddrSetPort(&addr, port);

            const QUIC_BUFFER alpnBufferList[] = {
                { (uint32_t)alpn.length(), (uint8_t*)alpn.c_str() }
            };

            status = MsQuic->ListenerStart(listener, alpnBufferList, 1, &addr);

            if (QUIC_FAILED(status)) {
                LOG_ERROR("initialize failed: ListenerStart status=0x%08X", status);
                return false;
            }

            initialized = true;
            LOG_INFO("MsquicServer Protocol: %s Accept Port: %d  initialize SUCCESS", alpn.c_str(), port);
            return true;
        }

        MsQuicRegistration* MsquicServer::getRegistration()
        {
            return registration;
        }

        MsQuicConfiguration* MsquicServer::getConfiguration()
        {
            return configuration;
        }

        MsquicManager* MsquicServer::loadBalanceMsquicManger()
        {
            size_t index = loadBalancer.fetch_add(1) % size;
            return msquicManagers[index];
        }

        void MsquicServer::postTaskAsync(size_t channelIndex,
            std::function<boost::asio::awaitable<void>(MsquicManager*)> asyncHandle)
        {
            if (channelIndex >= msquicManagers.size()) {
                LOG_ERROR("Invalid channelIndex: %zu, size: %zu", channelIndex, msquicManagers.size());
                return;
            }

            auto manager = msquicManagers[channelIndex];
            if (!manager) {
                LOG_ERROR("MsquicManager at index %zu is null", channelIndex);
                return;
            }

            boost::asio::co_spawn(manager->getMsquicLogicSystem()->getIoCompletePorts(),
                [manager, asyncHandle = std::move(asyncHandle)]() -> boost::asio::awaitable<void> {
                    co_await asyncHandle(manager);
                }, [this](std::exception_ptr ptr) {
                    if (ptr) {
                        try {
                            std::rethrow_exception(ptr);
                        }
                        catch (const std::exception& e) {
                            LOG_ERROR("MsquicServer boost::asio::co_spawn Exception: %s", e.what());
                        }
                    }
                    });
        }


        QUIC_STATUS QUIC_API MsquicAcceptHandle(HQUIC listener, void* context, QUIC_LISTENER_EVENT* event)
        {
            hope::quic::MsquicServer* server = static_cast<hope::quic::MsquicServer*>(context);

            if (server == nullptr) {
                LOG_ERROR("MsquicAcceptHandle: server context is null");
                return QUIC_STATUS_INVALID_PARAMETER;
            }

            if (event == nullptr) {
                LOG_ERROR("MsquicAcceptHandle: event is null");
                return QUIC_STATUS_INVALID_PARAMETER;
            }

            switch (event->Type) {
            case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
                if (event->NEW_CONNECTION.Connection == nullptr) {
                    LOG_ERROR("NEW_CONNECTION: Connection handle is null");
                    return QUIC_STATUS_INVALID_PARAMETER;
                }

                MsQuicConfiguration* config = server->getConfiguration();
                if (config == nullptr) {
                    LOG_ERROR("Configuration is null!");
                    return QUIC_STATUS_INVALID_PARAMETER;
                }

                QUIC_STATUS status = MsQuic->ConnectionSetConfiguration(
                    event->NEW_CONNECTION.Connection,
                    *config);

                if (QUIC_FAILED(status)) {
                    LOG_ERROR("ConnectionSetConfiguration FAILED with status 0x%08X", status);
                    return QUIC_STATUS_ABORTED;
                }

                MsquicManager * msquicManager = server->loadBalanceMsquicManger();

                MsquicSocket* msquicSocket = new MsquicSocket(event->NEW_CONNECTION.Connection,
                    msquicManager,
                    msquicManager->getMsquicLogicSystem()->getIoCompletePorts());

                msquicSocket->runEventLoop();

                MsQuic->SetCallbackHandler(
                    event->NEW_CONNECTION.Connection,
                    MsquicConnectionHandle,
                    msquicSocket);

                return status;
            }

            default:
                break;
            }

            return QUIC_STATUS_SUCCESS;
        }


        QUIC_STATUS QUIC_API MsquicConnectionHandle(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event) {

            MsquicSocket* msquicSocket = static_cast<MsquicSocket*>(context);

            if (msquicSocket == nullptr) {
                LOG_ERROR("MsquicConnectionHandle: server context is null");
                return QUIC_STATUS_INVALID_PARAMETER;
            }

            if (event == nullptr) {
                LOG_ERROR("MsquicConnectionHandle: event is null");
                return QUIC_STATUS_INVALID_PARAMETER;
            }

            switch (event->Type) {
            case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            {
                if (msquicSocket) {

                    msquicSocket->shutDown();

                    boost::asio::co_spawn(msquicSocket->getIoCompletionPorts(), [=]()mutable->boost::asio::awaitable<void> {
                        
                        msquicSocket->getMsquicManager()->removeConnection(msquicSocket->getAccountId());

                        delete msquicSocket;

                        msquicSocket = nullptr;

                        co_return;

                        }, boost::asio::detached);

                }
     
                break;
            }

            case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            {
                msquicSocket->setRemoteStream(event->PEER_STREAM_STARTED.Stream);

                MsQuic->SetCallbackHandler(
                    event->PEER_STREAM_STARTED.Stream,
                    hope::quic::MsquicSocketHandle,
                    msquicSocket);
                break;
            }

            default:
                break;
            }
            return QUIC_STATUS_SUCCESS;
        }

    }

}