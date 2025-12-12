#define QUIC_API_ENABLE_PREVIEW_FEATURES 1
#define QUIC_VERSION_2          0x6b3343cfU     // Second official version (host byte order)
#define QUIC_VERSION_1          0x00000001U     // First official version (host byte order)

#include <msquic.h>

#include "MsquicServer.h"
#include "MsquicManager.h"
#include "MsquicSocket.h"
#include "MsQuicApi.h"

#include "AsioProactors.h"
#include "ConfigManager.h"

#include "Utils.h"

namespace hope {

    namespace quic {


        // Constants definition
        const uint32_t supportedVersions[] = { QUIC_VERSION_1, QUIC_VERSION_2 };

        const MsQuicVersionSettings versionSettings(supportedVersions, 2);

        MsquicServer::MsquicServer(size_t port, std::string alpn, size_t size)
            : port(port)
            , alpn(alpn)
            , size(size)
            , msquicManagers(size)
            , iocpThreads(size){

            for (int i = 0; i < size; i++) {
                std::pair<size_t, boost::asio::io_context&> pairs =
                    hope::iocp::AsioProactors::getInstance()->getIoCompletePorts();
                msquicManagers[i] = std::make_shared<MsquicManager>(pairs.first, pairs.second, this);
            }

        }

        MsquicServer::~MsquicServer()
        {
            shutDown();

        }

        bool MsquicServer::RunEventLoop() {
        
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

            LOG_INFO("MsquicServer Protocol: %s Accept Port: %d  initialize SUCCESS", alpn.c_str(), port);
            return true;

        }

        bool MsquicServer::initialize()
        {

            if (initialize) return true;
            
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

            executions = new QUIC_EXECUTION * [size];

            // 3. 确保 ioCompletionPorts 也是动态分配的，防止越界
            ioCompletionPorts = new HANDLE[size];

            configs = new QUIC_EXECUTION_CONFIG[size];

            for (int i = 0; i < size; i++) {

                HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

                ioCompletionPorts[i] = iocp;

                configs[i].IdealProcessor = i;

                // 这里取数组中元素的地址，是安全的（只要 ioCompletionPorts 没被释放）
                configs[i].EventQ = &ioCompletionPorts[i];
            }

            QUIC_STATUS status = MsQuic->ExecutionCreate(
                QUIC_GLOBAL_EXECUTION_CONFIG_FLAG_HIGH_PRIORITY,
                1000,
                size,
                configs,
                executions 
            );

            if (QUIC_FAILED(status)) {

                LOG_ERROR("Msquic->ExecutionCreate Error: 0x%08X", status);

                return false;
            }

            iocpRunEvent.store(true);
            
            for (int i = 0; i < size; i++) {

                iocpThreads.emplace_back(std::thread([this,i]() {
                    
                    while (iocpRunEvent.load()) {
                    
                        uint32_t WaitTime = MsQuic->ExecutionPoll(executions[i]);

                        ULONG OverlappedCount = 0;

                        OVERLAPPED_ENTRY Overlapped[8];

                        if (GetQueuedCompletionStatusEx(ioCompletionPorts[i], Overlapped, ARRAYSIZE(Overlapped), &OverlappedCount, WaitTime, FALSE)) {

                            for (ULONG i = 0; i < OverlappedCount; ++i) {

                                if (Overlapped[i].lpOverlapped == NULL) {

                                    continue;
                                }

                                QUIC_SQE* Sqe = CONTAINING_RECORD(Overlapped[i].lpOverlapped, QUIC_SQE, Overlapped);

                                Sqe->Completion(&Overlapped[i]);

                            }

                        }

                    }

                    }));

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

            initialized = true;
            return true;
        }

        void MsquicServer::shutDown()
        {
            // 防止重复调用
            if (!initialized) return;

            LOG_INFO("MsquicServer shutting down...");

            // 1. 关闭 Listener
            if (listener != nullptr) {
                MsQuic->ListenerStop(listener);
                // ListenerClose 会阻塞直到所有待处理的 Listener 事件处理完毕
                MsQuic->ListenerClose(listener);
                listener = nullptr;
            }

            msquicManagers.clear();

            // 3. 关闭 Registration 和 Configuration
            // 这些对象通常不产生异步回调，但按顺序关闭是个好习惯
            if (configuration != nullptr) {
                delete configuration;
                configuration = nullptr; // MsQuicConfiguration 包装类如果内部调了 Close 则 delete 也可以
            }

            if (registration != nullptr) {
                // RegistrationClose 应该在所有子对象(Conns/Config)关闭后调用
                // 你的 MsQuicRegistration 包装类析构函数里应该调用了 RegistrationClose
                delete registration;
                registration = nullptr;
            }

            // ---------------------------------------------------------
            // 第二阶段：停止执行引擎 (IOCP 线程)
            // ---------------------------------------------------------
            // 此时 MsQuic 已经完全关闭，不再会有新的事件投递到 IOCP

            // 1. 设置退出标志
            iocpRunEvent.store(false);

            // 2. 唤醒所有卡在 GetQueuedCompletionStatusEx 的线程
            if (ioCompletionPorts) {
                for (int i = 0; i < size; i++) {
                    // 发送一个特殊的空包，让线程从阻塞中醒来并检查 iocpRunEvent
                    PostQueuedCompletionStatus(ioCompletionPorts[i], 0, 0, NULL);
                }
            }

            // 3. 等待线程真正退出
            for (auto& t : iocpThreads) {
                if (t.joinable()) {
                    t.join();
                }
            }
            iocpThreads.clear();

            // ---------------------------------------------------------
            // 第三阶段：释放底层系统资源
            // ---------------------------------------------------------

            // 1. 关闭 IOCP 句柄
            if (ioCompletionPorts) {
                for (int i = 0; i < size; i++) {
                    CloseHandle(ioCompletionPorts[i]);
                }
                delete[] ioCompletionPorts;
                ioCompletionPorts = nullptr;
            }

            // 2. 释放配置数组
            // MsQuic 已经不再使用这些 Config 了，可以安全释放
            if (executions) {
                delete[] executions;
                executions = nullptr;
            }

            if (configs) {
                delete[] configs;
                configs = nullptr;
            }

            initialized = false;

            LOG_INFO("MsquicServer shutdown complete.");
        }

        MsQuicRegistration* MsquicServer::getRegistration()
        {
            return registration;
        }

        MsQuicConfiguration* MsquicServer::getConfiguration()
        {
            return configuration;
        }

        std::shared_ptr<MsquicManager> MsquicServer::loadBalanceMsquicManger()
        {
            size_t index = loadBalancer.fetch_add(1) % size;
            return msquicManagers[index];
        }

        void MsquicServer::postTaskAsync(size_t channelIndex,
            std::function<boost::asio::awaitable<void>(std::shared_ptr<MsquicManager>)> asyncHandle)
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
                [sharedManager = manager->shared_from_this(), asyncHandle = std::move(asyncHandle)]() -> boost::asio::awaitable<void> {
                    co_await asyncHandle(sharedManager);
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

                std::shared_ptr<MsquicManager> msquicManager = server->loadBalanceMsquicManger();

                std::shared_ptr<MsquicSocket> msquicSocket = std::make_shared<MsquicSocket>(event->NEW_CONNECTION.Connection,
                    msquicManager.get(),
                    msquicManager->getMsquicLogicSystem()->getIoCompletePorts());

                msquicSocket->runEventLoop();

                MsQuic->SetCallbackHandler(
                    event->NEW_CONNECTION.Connection,
                    MsquicConnectionHandle,
                    msquicSocket.get());

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