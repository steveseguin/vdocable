#include "router/webrtc/audio_webrtc_client.h"

#include <rtc/common.hpp>
#include <rtc/rtc.hpp>
#include <rtc/rtppacketizer.hpp>

#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace router::webrtc {
namespace {

constexpr uint8_t kAudioPayloadType = 111;
constexpr uint32_t kAudioClockRate = rtc::OpusRtpPacketizer::DefaultClockRate;

rtc::binary toBinary(const std::vector<uint8_t> &data) {
    rtc::binary out;
    out.reserve(data.size());
    for (uint8_t byte : data) {
        out.push_back(static_cast<rtc::byte>(byte));
    }
    return out;
}

}  // namespace

struct AudioWebRtcClient::Impl {
    rtc::Configuration config;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> audioTrack;
    std::shared_ptr<rtc::RtpPacketizer> audioPacketizer;
    std::shared_ptr<rtc::RtpPacketizationConfig> audioRtpConfig;
    std::string localDescription;
    IceCandidateCallback iceCallback;
    StateCallback stateCallback;
    std::mutex descMutex;
    std::atomic<ConnectionState> state{ConnectionState::Disconnected};
    std::atomic<bool> gatheringComplete{false};
    std::atomic<bool> audioTrackOpen{false};
    uint32_t audioSsrc = 3333333;

    void setupTrack() {
        audioTrackOpen.store(false);

        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(kAudioPayloadType);
        audio.addSSRC(audioSsrc, "vdocable");
        audioTrack = pc->addTrack(audio);

        audioTrack->onOpen([this]() {
            spdlog::info("[AudioWebRTC] Audio track opened");
            audioTrackOpen.store(true);
        });
        audioTrack->onClosed([this]() {
            spdlog::info("[AudioWebRTC] Audio track closed");
            audioTrackOpen.store(false);
        });

        audioRtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            audioSsrc,
            "vdocable",
            kAudioPayloadType,
            kAudioClockRate);
        audioPacketizer = std::make_shared<rtc::OpusRtpPacketizer>(audioRtpConfig);
        auto audioReporter = std::make_shared<rtc::RtcpSrReporter>(audioRtpConfig);
        auto audioNack = std::make_shared<rtc::RtcpNackResponder>();
        audioPacketizer->addToChain(audioReporter);
        audioPacketizer->addToChain(audioNack);
        audioTrack->setMediaHandler(audioPacketizer);
    }
};

AudioWebRtcClient::AudioWebRtcClient() : impl_(std::make_unique<Impl>()) {}
AudioWebRtcClient::~AudioWebRtcClient() { shutdown(); }

bool AudioWebRtcClient::initialize(const PeerConfig &config) {
    rtc::Configuration rtcConfig;
    for (const auto &ice : config.iceServers) {
        rtcConfig.iceServers.emplace_back(ice);
    }

    impl_->pc = std::make_shared<rtc::PeerConnection>(rtcConfig);

    impl_->pc->onStateChange([this](rtc::PeerConnection::State state) {
        const char *stateStr = "unknown";
        ConnectionState mapped = ConnectionState::Disconnected;
        if (state == rtc::PeerConnection::State::Connecting) {
            mapped = ConnectionState::Connecting;
            stateStr = "connecting";
        } else if (state == rtc::PeerConnection::State::Connected) {
            mapped = ConnectionState::Connected;
            stateStr = "connected";
        } else if (state == rtc::PeerConnection::State::Failed) {
            mapped = ConnectionState::Failed;
            stateStr = "failed";
        } else if (state == rtc::PeerConnection::State::Closed) {
            stateStr = "closed";
        } else if (state == rtc::PeerConnection::State::Disconnected) {
            stateStr = "disconnected";
        } else if (state == rtc::PeerConnection::State::New) {
            stateStr = "new";
        }
        spdlog::info("[AudioWebRTC] PeerConnection state: {}", stateStr);
        impl_->state.store(mapped);
        if (impl_->stateCallback) {
            impl_->stateCallback(mapped);
        }
    });

    impl_->pc->onLocalCandidate([this](rtc::Candidate candidate) {
        if (impl_->iceCallback) {
            impl_->iceCallback(candidate.candidate(), candidate.mid(), 0);
        }
    });

    impl_->pc->onLocalDescription([this](rtc::Description desc) {
        std::lock_guard<std::mutex> lock(impl_->descMutex);
        impl_->localDescription = std::string(desc);
    });

    impl_->pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            impl_->gatheringComplete.store(true);
        }
    });

    impl_->setupTrack();
    return true;
}

void AudioWebRtcClient::shutdown() {
    if (impl_->pc) {
        impl_->pc->close();
        impl_->pc.reset();
    }
    impl_->audioTrack.reset();
    impl_->audioPacketizer.reset();
    impl_->audioRtpConfig.reset();
    impl_->audioTrackOpen.store(false);
    impl_->gatheringComplete.store(false);
    impl_->localDescription.clear();
}

bool AudioWebRtcClient::setRemoteDescription(const std::string &sdp, const std::string &type) {
    if (!impl_->pc) {
        return false;
    }
    rtc::Description::Type descType = rtc::Description::Type::Offer;
    if (type == "answer") {
        descType = rtc::Description::Type::Answer;
    }
    impl_->pc->setRemoteDescription(rtc::Description(sdp, descType));
    return true;
}

std::string AudioWebRtcClient::createOffer() {
    if (!impl_->pc) {
        return {};
    }
    impl_->localDescription.clear();
    impl_->gatheringComplete.store(false);
    impl_->pc->setLocalDescription(rtc::Description::Type::Offer);

    auto start = std::chrono::steady_clock::now();
    while (!impl_->gatheringComplete.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            spdlog::warn("[AudioWebRTC] ICE gathering timeout; using partial candidates");
            break;
        }
    }

    auto desc = impl_->pc->localDescription();
    if (desc) {
        return std::string(*desc);
    }
    return impl_->localDescription;
}

std::string AudioWebRtcClient::createAnswer(const std::string &offer) {
    if (!impl_->pc) {
        return {};
    }
    impl_->localDescription.clear();
    impl_->pc->setRemoteDescription(rtc::Description(offer, rtc::Description::Type::Offer));
    impl_->pc->setLocalDescription(rtc::Description::Type::Answer);

    auto start = std::chrono::steady_clock::now();
    while (impl_->localDescription.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            break;
        }
    }
    return impl_->localDescription;
}

void AudioWebRtcClient::addRemoteCandidate(const std::string &candidate, const std::string &mid, int) {
    if (!impl_->pc) {
        return;
    }
    impl_->pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
}

void AudioWebRtcClient::setIceCandidateCallback(IceCandidateCallback cb) {
    impl_->iceCallback = std::move(cb);
}

void AudioWebRtcClient::setStateCallback(StateCallback cb) {
    impl_->stateCallback = std::move(cb);
}

bool AudioWebRtcClient::sendAudio(const EncodedAudioPacket &packet) {
    if (!impl_->audioTrack || !impl_->audioTrack->isOpen()) {
        return false;
    }
    if (packet.data.empty()) {
        return false;
    }

    const uint32_t rtpTimestamp = static_cast<uint32_t>((packet.pts * 48) / 10000);
    impl_->audioRtpConfig->timestamp = rtpTimestamp;
    impl_->audioTrack->send(toBinary(packet.data));
    return true;
}

bool AudioWebRtcClient::hasActiveAudioTrack() const {
    return impl_->audioTrackOpen.load();
}

}  // namespace router::webrtc
