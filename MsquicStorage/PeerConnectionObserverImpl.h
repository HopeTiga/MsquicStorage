#pragma once

#include <api/peer_connection_interface.h>

namespace hope {

	namespace rtc {

        class WebRTCManager;

        class PeerConnectionObserverImpl : public webrtc::PeerConnectionObserver {

        public:

            explicit PeerConnectionObserverImpl(WebRTCManager* manager) : manager(manager) {}

            void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState) override;

            void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override;

            void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState newState) override;

            void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;

            void OnAddStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}

            void OnRemoveStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}

            void OnRenegotiationNeeded() override {}

            void OnNegotiationNeededEvent(uint32_t eventId) override {}

            void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState newState) override;

            void OnStandardizedIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState newState) override {}

            void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState newState) override;

            void OnIceCandidateError(const std::string& address, int port, const std::string& url,
                int errorCode, const std::string& errorText) override;

            void OnIceCandidateRemoved(const webrtc::IceCandidate* candidate) override {}

            void OnIceCandidatesRemoved(const std::vector<webrtc::Candidate>& candidates) override {}

            void OnIceConnectionReceivingChange(bool receiving) override {}

            void OnIceSelectedCandidatePairChanged(const webrtc::CandidatePairChangeEvent& event) override {}

            void OnAddTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override {
            }

            void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {}

            void OnRemoveTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

            void OnInterestingUsage(int usagePattern) override {}

        private:

            WebRTCManager* manager;

        };


	}

}

