#include "PeerConnectionObserverImpl.h"

#include <boost/json.hpp>

#include "WebRTCManager.h"

#include "Utils.h"

namespace hope {

	namespace rtc {
	
        // Observer实现
        void PeerConnectionObserverImpl::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState) {
            switch (newState) {
            case webrtc::PeerConnectionInterface::kClosed: {
                LOG_INFO("Signaling state: kClosed");
                break;
            }
            default:
                break;
            }
        }

        void PeerConnectionObserverImpl::OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {
        }

        void PeerConnectionObserverImpl::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState newState) {
            if (newState == webrtc::PeerConnectionInterface::kIceGatheringComplete) {
                LOG_INFO("ICE gathering complete");
            }
        }

        void PeerConnectionObserverImpl::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
            if (!candidate) {
                LOG_ERROR("OnIceCandidate called with null candidate");
                return;
            }

            std::string sdp;
            if (!candidate->ToString(&sdp)) {
                LOG_ERROR("Failed to convert ICE candidate to string");
                return;
            }

            boost::json::object msg;
            msg["type"] = "candidate";
            msg["candidate"] = sdp;
            msg["mid"] = candidate->sdp_mid();
            msg["mlineIndex"] = candidate->sdp_mline_index();

            manager->sendSignalingMessage(msg);
        }

        void PeerConnectionObserverImpl::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState newState) {
            switch (newState) {
            case webrtc::PeerConnectionInterface::kIceConnectionConnected:
                LOG_INFO("ICE connection established");
                break;
            case webrtc::PeerConnectionInterface::kIceConnectionFailed:
                LOG_ERROR("ICE connection failed");
                break;
            default:
                break;
            }
        }

        void PeerConnectionObserverImpl::OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState newState) {
            switch (newState) {
            case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected: {

                LOG_INFO("Peer connection established");

                auto localDesc = manager->peerConnection->local_description();
                if (localDesc) {
                    std::string sdp;
                    localDesc->ToString(&sdp);

                    // 简单解析SDP找到第一个video编解码器
                    std::istringstream stream(sdp);
                    std::string line;
                    while (std::getline(stream, line)) {
                        if (line.find("a=rtpmap:") == 0 && line.find("/90000") != std::string::npos) {
                            // 找到视频编解码器行，提取编解码器名称
                            size_t spacePos = line.find(' ');
                            if (spacePos != std::string::npos) {
                                std::string codecInfo = line.substr(spacePos + 1);
                                size_t slashPos = codecInfo.find('/');
                                if (slashPos != std::string::npos) {
                                    std::string codecName = codecInfo.substr(0, slashPos);
                                    LOG_INFO("=== Video codec actually being used: %s ===", codecName.c_str());
                                    break;
                                }
                            }
                        }
                    }
                }

                boost::json::object json;

                json["requestType"] = static_cast<int64_t>(WebRTCRequestState::START);

                std::string jsonStr = boost::json::serialize(json);

                std::shared_ptr<WriterData> data = std::make_shared<WriterData>(const_cast<char*>(jsonStr.c_str()), jsonStr.size());

                manager->writerAsync(data);

                break;
            }
            case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed: {

                LOG_ERROR("Peer connection failed");

                boost::json::object json;

                json["requestType"] = static_cast<int64_t>(WebRTCRequestState::CLOSE);

                std::string jsonStr = boost::json::serialize(json);

                std::shared_ptr<WriterData> data = std::make_shared<WriterData>(const_cast<char*>(jsonStr.c_str()), jsonStr.size());

                manager->writerAsync(data);

                break;
            }
            case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed: {

                break;
            }
            default:
                break;
            }
        }

        void PeerConnectionObserverImpl::OnIceCandidateError(const std::string& address, int port, const std::string& url,
            int errorCode, const std::string& errorText) {
        
            printf("PeerConnectionObserverImpl::OnIceCandidateError: address=%s, port=%d, url=%s, errorCode=%d, errorText=%s\n",
                address.c_str(), port, url.c_str(), errorCode, errorText.c_str());

        }

	}

}