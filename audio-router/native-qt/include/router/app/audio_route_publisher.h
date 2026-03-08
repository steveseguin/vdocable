#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "router/app/source_info.h"
#include "router/webrtc/audio_webrtc_client.h"
#include "versus/audio/opus_encoder.h"
#include "versus/audio/window_audio_capture_core.h"
#include "versus/signaling/vdo_signaling.h"

namespace router::app {

struct StartOptions {
    std::string room;
    std::string password;
    std::string label;
    std::string streamId;
    std::string server = "wss://wss.vdo.ninja";
    std::string salt = "vdo.ninja";
    int maxViewers = 8;
};

class AudioRoutePublisher {
  public:
    AudioRoutePublisher();
    ~AudioRoutePublisher();

    static std::vector<SourceInfo> listAudioSources();
    static std::string formatSourceLabel(const SourceInfo &source);

    bool start(const SourceInfo &source, const StartOptions &options);
    void stop();

    bool isLive() const;
    float audioLevelRms() const;
    float audioPeak() const;
    std::string status() const;
    std::string shareLink() const;
    std::string lastError() const;
    uint32_t sourceProcessId() const;

  private:
    struct PendingCandidate {
        std::string candidate;
        std::string mid;
        int mlineIndex = 0;
    };

    struct PeerSession {
        std::string uuid;
        std::string session;
        std::string streamId;
        std::string candidateType = "local";
        bool answerReceived = false;
        std::vector<PendingCandidate> pendingCandidates;
        std::unique_ptr<router::webrtc::AudioWebRtcClient> client;
    };

    void setupCallbacks();
    void setupSignalingCallbacks();
    void handleAudioChunk(versus::audio::StreamChunk &&chunk);
    bool hasAnyActiveAudioTrack() const;
    void sendAudioPacketToPeers(const router::webrtc::EncodedAudioPacket &packet);
    std::string makePeerKey(const std::string &uuid, const std::string &session) const;
    void removePeerSession(const std::string &uuid, const std::string &session);
    void clearPeerSessions();
    void disconnectSignaling(bool sendUnpublish);
    bool connectAndPublish();
    void handleStreamIdCollision(const std::string &message);
    void startSignalingRecovery();
    void stopSignalingRecoveryThread();
    void setStatus(const std::string &value);
    void setError(const std::string &value);
    void setShareLink(const std::string &value);
    void teardownCapture();

    std::atomic<bool> live_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> captureReady_{false};
    std::atomic<bool> reconnecting_{false};
    std::atomic<bool> duplicateStreamId_{false};
    std::atomic<int64_t> audioPts100ns_{0};
    std::atomic<float> audioLevelRms_{0.0f};
    std::atomic<float> audioPeak_{0.0f};
    SourceInfo source_;
    StartOptions startOptions_;
    std::string streamId_;
    mutable std::mutex stateMutex_;
    std::string status_ = "Stopped";
    std::string shareLink_;
    std::string lastError_;
    mutable std::mutex peerSessionsMutex_;
    mutable std::mutex signalingMutex_;
    std::thread signalingRecoveryThread_;
    std::unordered_map<std::string, std::shared_ptr<PeerSession>> peerSessions_;
    versus::audio::WindowAudioCaptureCore audioCapture_;
    versus::audio::OpusEncoder opusEncoder_;
    versus::signaling::VdoSignaling signaling_;
};

}  // namespace router::app
