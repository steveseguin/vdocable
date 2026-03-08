#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace router::webrtc {

struct EncodedAudioPacket {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    uint32_t sampleRate = 48000;
    uint16_t channels = 2;
};

struct PeerConfig {
    std::vector<std::string> iceServers;
};

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Failed
};

class AudioWebRtcClient {
  public:
    using IceCandidateCallback = std::function<void(const std::string &candidate, const std::string &mid, int mlineIndex)>;
    using StateCallback = std::function<void(ConnectionState)>;

    AudioWebRtcClient();
    ~AudioWebRtcClient();

    bool initialize(const PeerConfig &config);
    void shutdown();

    bool setRemoteDescription(const std::string &sdp, const std::string &type);
    std::string createOffer();
    std::string createAnswer(const std::string &offer);
    void addRemoteCandidate(const std::string &candidate, const std::string &mid, int mlineIndex);

    void setIceCandidateCallback(IceCandidateCallback cb);
    void setStateCallback(StateCallback cb);

    bool sendAudio(const EncodedAudioPacket &packet);
    bool hasActiveAudioTrack() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace router::webrtc
