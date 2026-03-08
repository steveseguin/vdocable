#include "router/app/audio_route_publisher.h"

#include "router/util/router_text.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

namespace router::app {
namespace {

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}
}  // namespace

AudioRoutePublisher::AudioRoutePublisher() {
    setupCallbacks();
}

AudioRoutePublisher::~AudioRoutePublisher() {
    stop();
}

std::vector<SourceInfo> AudioRoutePublisher::listAudioSources() {
    versus::audio::WindowAudioCaptureCore capture;
    const auto sessions = capture.GetAudioSessions();

    std::map<uint32_t, SourceInfo> deduped;
    for (const auto &session : sessions) {
        if (session.processId == 0) {
            continue;
        }

        auto &entry = deduped[session.processId];
        entry.processId = session.processId;
        entry.active = entry.active || session.active;

        const std::string displayName = router::util::trimCopy(session.displayName);
        const std::string executableName = router::util::trimCopy(session.executableName);
        if (!displayName.empty() && (entry.displayName.empty() || session.active)) {
            entry.displayName = displayName;
        }
        if (!executableName.empty()) {
            entry.executableName = executableName;
        }
    }

    std::vector<SourceInfo> sources;
    sources.reserve(deduped.size());
    for (const auto &[_, source] : deduped) {
        sources.push_back(source);
    }

    std::sort(sources.begin(), sources.end(), [](const SourceInfo &a, const SourceInfo &b) {
        if (a.active != b.active) {
            return a.active > b.active;
        }
        return router::util::preferredDisplayName(a) < router::util::preferredDisplayName(b);
    });
    return sources;
}

std::string AudioRoutePublisher::formatSourceLabel(const SourceInfo &source) {
    return router::util::formatSourceLabel(source);
}

bool AudioRoutePublisher::start(const SourceInfo &source, const StartOptions &options) {
    stop();

    source_ = source;
    startOptions_ = options;
    streamId_.clear();
    stopRequested_.store(false, std::memory_order_relaxed);
    captureReady_.store(false, std::memory_order_relaxed);
    reconnecting_.store(false, std::memory_order_relaxed);
    duplicateStreamId_.store(false, std::memory_order_relaxed);
    audioPts100ns_.store(0, std::memory_order_relaxed);
    audioLevelRms_.store(0.0f, std::memory_order_relaxed);
    audioPeak_.store(0.0f, std::memory_order_relaxed);
    setShareLink("");
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_.clear();
    }

    setStatus("Starting capture...");
    const auto captureResult = audioCapture_.StartStreamCapture(source_.processId, [this](versus::audio::StreamChunk &&chunk) {
        handleAudioChunk(std::move(chunk));
    });
    if (!captureResult.success) {
        setError(captureResult.error.empty() ? "Failed to start process audio capture." : captureResult.error);
        teardownCapture();
        return false;
    }

    versus::audio::AudioEncoderConfig audioConfig;
    audioConfig.sampleRate = static_cast<int>(captureResult.sampleRate);
    audioConfig.channels = static_cast<int>(captureResult.channels);
    audioConfig.bitrate = 192;
    if (!opusEncoder_.initialize(audioConfig)) {
        setError("Failed to initialize Opus encoder.");
        teardownCapture();
        return false;
    }
    captureReady_.store(true, std::memory_order_relaxed);

    setupSignalingCallbacks();
    setStatus("Connecting to signaling...");
    if (!connectAndPublish()) {
        teardownCapture();
        return false;
    }

    live_.store(true, std::memory_order_relaxed);
    setStatus("Live");
    return true;
}

void AudioRoutePublisher::stop() {
    stopRequested_.store(true, std::memory_order_relaxed);
    live_.store(false, std::memory_order_relaxed);
    captureReady_.store(false, std::memory_order_relaxed);
    reconnecting_.store(false, std::memory_order_relaxed);
    duplicateStreamId_.store(false, std::memory_order_relaxed);
    stopSignalingRecoveryThread();

    disconnectSignaling(true);

    clearPeerSessions();
    teardownCapture();
    audioLevelRms_.store(0.0f, std::memory_order_relaxed);
    audioPeak_.store(0.0f, std::memory_order_relaxed);
    setShareLink("");
    setStatus("Stopped");
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_.clear();
    }
}

bool AudioRoutePublisher::isLive() const {
    return live_.load(std::memory_order_relaxed);
}

float AudioRoutePublisher::audioLevelRms() const {
    return audioLevelRms_.load(std::memory_order_relaxed);
}

float AudioRoutePublisher::audioPeak() const {
    return audioPeak_.load(std::memory_order_relaxed);
}

std::string AudioRoutePublisher::status() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return status_;
}

std::string AudioRoutePublisher::shareLink() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return shareLink_;
}

std::string AudioRoutePublisher::lastError() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastError_;
}

uint32_t AudioRoutePublisher::sourceProcessId() const {
    return source_.processId;
}

void AudioRoutePublisher::setupCallbacks() {
    opusEncoder_.setPacketCallback([this](const versus::audio::EncodedAudioPacket &packet) {
        router::webrtc::EncodedAudioPacket out;
        out.data = packet.data;
        out.pts = packet.pts;
        out.sampleRate = static_cast<uint32_t>(packet.sampleRate);
        out.channels = static_cast<uint16_t>(packet.channels);
        sendAudioPacketToPeers(out);
    });
}

void AudioRoutePublisher::setupSignalingCallbacks() {
    signaling_.onDisconnected([this]() {
        if (stopRequested_.load(std::memory_order_relaxed) || duplicateStreamId_.load(std::memory_order_relaxed)) {
            return;
        }
        startSignalingRecovery();
    });

    signaling_.onError([this](const std::string &error) {
        if (stopRequested_.load(std::memory_order_relaxed) || duplicateStreamId_.load(std::memory_order_relaxed)) {
            return;
        }
        if (signaling_.isConnected()) {
            spdlog::warn("[AudioRoute] Ignoring non-fatal signaling error while websocket remains connected: {}",
                         error.empty() ? "(empty)" : error);
            return;
        }
        startSignalingRecovery();
    });

    signaling_.onAlert([this](const std::string &message) {
        if (router::util::isStreamIdInUseAlert(message)) {
            handleStreamIdCollision(message);
            return;
        }
        if (!message.empty()) {
            setStatus(message);
        }
    });

    signaling_.onOfferRequest([this](const std::string &uuid, const std::string &session, const std::string &streamId) {
        const std::string resolvedSession = session.empty()
            ? ("session_" + std::to_string(steadyNowMs()))
            : session;
        const std::string resolvedStreamId = streamId_.empty() ? streamId : streamId_;
        const std::string key = makePeerKey(uuid, resolvedSession);

        if (startOptions_.maxViewers > 0) {
            std::lock_guard<std::mutex> lock(peerSessionsMutex_);
            if (peerSessions_.find(key) == peerSessions_.end() &&
                static_cast<int>(peerSessions_.size()) >= startOptions_.maxViewers) {
                spdlog::warn("[AudioRoute] Viewer limit reached for {}", streamId_);
                return;
            }
        }

        std::shared_ptr<PeerSession> replacedPeer;
        {
            std::lock_guard<std::mutex> lock(peerSessionsMutex_);
            const auto existing = peerSessions_.find(key);
            if (existing != peerSessions_.end()) {
                replacedPeer = existing->second;
                peerSessions_.erase(existing);
            }
        }
        if (replacedPeer && replacedPeer->client) {
            replacedPeer->client->shutdown();
        }

        auto peer = std::make_shared<PeerSession>();
        peer->uuid = uuid;
        peer->session = resolvedSession;
        peer->streamId = resolvedStreamId;
        peer->client = std::make_unique<router::webrtc::AudioWebRtcClient>();

        router::webrtc::PeerConfig config;
        config.iceServers = {"stun:stun.l.google.com:19302"};
        if (!peer->client->initialize(config)) {
            spdlog::error("[AudioRoute] Failed to initialize peer session");
            return;
        }

        std::weak_ptr<PeerSession> weakPeer = peer;
        peer->client->setStateCallback([this, weakPeer](router::webrtc::ConnectionState state) {
            auto peerPtr = weakPeer.lock();
            if (!peerPtr) {
                return;
            }
            if (state == router::webrtc::ConnectionState::Connected) {
                setStatus("Live");
                return;
            }
            if (state == router::webrtc::ConnectionState::Disconnected ||
                state == router::webrtc::ConnectionState::Failed) {
                removePeerSession(peerPtr->uuid, peerPtr->session);
            }
        });

        peer->client->setIceCandidateCallback([this, weakPeer](const std::string &candidate,
                                                               const std::string &mid,
                                                               int mlineIndex) {
            auto peerPtr = weakPeer.lock();
            if (!peerPtr || candidate.empty()) {
                return;
            }

            const std::string lowerCandidate = toLowerCopy(candidate);
            const bool relayCandidate = lowerCandidate.find(" typ relay") != std::string::npos;

            bool shouldSend = false;
            std::string sessionLocal;
            std::string uuidLocal;
            std::string typeLocal;
            {
                std::lock_guard<std::mutex> lock(peerSessionsMutex_);
                const auto it = peerSessions_.find(makePeerKey(peerPtr->uuid, peerPtr->session));
                if (it == peerSessions_.end() || !it->second) {
                    return;
                }

                auto &peerState = it->second;
                if (!peerState->answerReceived) {
                    peerState->pendingCandidates.push_back({candidate, mid, mlineIndex});
                    if (relayCandidate) {
                        peerState->candidateType = "relay";
                    }
                    return;
                }

                shouldSend = true;
                sessionLocal = peerState->session;
                uuidLocal = peerState->uuid;
                if (relayCandidate) {
                    peerState->candidateType = "relay";
                }
                typeLocal = peerState->candidateType;
            }

            if (!shouldSend) {
                return;
            }

            versus::signaling::SignalCandidate signalCandidate;
            signalCandidate.uuid = uuidLocal;
            signalCandidate.candidate = candidate;
            signalCandidate.mid = mid;
            signalCandidate.mlineIndex = mlineIndex;
            signalCandidate.session = sessionLocal;
            signalCandidate.type = typeLocal;
            std::lock_guard<std::mutex> lock(signalingMutex_);
            signaling_.sendCandidate(signalCandidate);
        });

        {
            std::lock_guard<std::mutex> lock(peerSessionsMutex_);
            peerSessions_[key] = peer;
        }

        const auto offerSdp = peer->client->createOffer();
        if (offerSdp.empty()) {
            removePeerSession(uuid, resolvedSession);
            return;
        }

        versus::signaling::SignalOffer offer;
        offer.uuid = uuid;
        offer.session = resolvedSession;
        offer.streamId = resolvedStreamId;
        offer.sdp = offerSdp;
        std::lock_guard<std::mutex> lock(signalingMutex_);
        signaling_.sendOffer(offer);
    });

    signaling_.onOffer([](const versus::signaling::SignalOffer &) {
    });

    signaling_.onAnswer([this](const versus::signaling::SignalAnswer &answer) {
        std::shared_ptr<PeerSession> peer;
        std::vector<PendingCandidate> buffered;
        std::string candidateType = "local";
        {
            std::lock_guard<std::mutex> lock(peerSessionsMutex_);
            const auto it = peerSessions_.find(makePeerKey(answer.uuid, answer.session));
            if (it == peerSessions_.end()) {
                return;
            }
            peer = it->second;
            peer->answerReceived = true;
            buffered = peer->pendingCandidates;
            peer->pendingCandidates.clear();
            candidateType = peer->candidateType;
        }

        peer->client->setRemoteDescription(answer.sdp, "answer");
        for (const auto &pending : buffered) {
            versus::signaling::SignalCandidate cand;
            cand.uuid = peer->uuid;
            cand.candidate = pending.candidate;
            cand.mid = pending.mid;
            cand.mlineIndex = pending.mlineIndex;
            cand.session = peer->session;
            cand.type = candidateType;
            std::lock_guard<std::mutex> lock(signalingMutex_);
            signaling_.sendCandidate(cand);
        }
    });

    signaling_.onCandidate([this](const versus::signaling::SignalCandidate &cand) {
        std::shared_ptr<PeerSession> peer;
        {
            std::lock_guard<std::mutex> lock(peerSessionsMutex_);
            const auto it = peerSessions_.find(makePeerKey(cand.uuid, cand.session));
            if (it == peerSessions_.end()) {
                return;
            }
            peer = it->second;
        }
        if (!peer || !peer->client) {
            return;
        }
        peer->client->addRemoteCandidate(cand.candidate, cand.mid, cand.mlineIndex);
    });
}

void AudioRoutePublisher::handleAudioChunk(versus::audio::StreamChunk &&chunk) {
    float peak = 0.0f;
    double sumSquares = 0.0;
    for (float sample : chunk.samples) {
        const float absSample = std::abs(sample);
        peak = std::max(peak, absSample);
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }

    const float rms = chunk.samples.empty()
        ? 0.0f
        : static_cast<float>(std::sqrt(sumSquares / static_cast<double>(chunk.samples.size())));
    const float prevRms = audioLevelRms_.load(std::memory_order_relaxed);
    audioLevelRms_.store(std::clamp((prevRms * 0.75f) + (rms * 0.25f), 0.0f, 1.0f), std::memory_order_relaxed);

    const float prevPeak = audioPeak_.load(std::memory_order_relaxed);
    audioPeak_.store(std::clamp(std::max(peak, prevPeak * 0.90f), 0.0f, 1.0f), std::memory_order_relaxed);

    if (!captureReady_.load(std::memory_order_relaxed) || !live_.load(std::memory_order_relaxed)) {
        return;
    }
    if (!hasAnyActiveAudioTrack()) {
        return;
    }

    const uint32_t channels = std::max<uint32_t>(1, chunk.channels);
    const uint32_t sampleRate = std::max<uint32_t>(1, chunk.sampleRate);
    const size_t frames = chunk.samples.size() / channels;
    const int64_t chunkDuration100ns =
        static_cast<int64_t>(frames) * 10000000LL / static_cast<int64_t>(sampleRate);
    const int64_t pts = audioPts100ns_.fetch_add(chunkDuration100ns, std::memory_order_relaxed);
    opusEncoder_.encode(chunk.samples, static_cast<int>(sampleRate), static_cast<int>(channels), pts);
}

bool AudioRoutePublisher::hasAnyActiveAudioTrack() const {
    std::lock_guard<std::mutex> lock(peerSessionsMutex_);
    for (const auto &entry : peerSessions_) {
        if (entry.second && entry.second->client && entry.second->client->hasActiveAudioTrack()) {
            return true;
        }
    }
    return false;
}

void AudioRoutePublisher::sendAudioPacketToPeers(const router::webrtc::EncodedAudioPacket &packet) {
    std::vector<std::shared_ptr<PeerSession>> peers;
    {
        std::lock_guard<std::mutex> lock(peerSessionsMutex_);
        peers.reserve(peerSessions_.size());
        for (const auto &entry : peerSessions_) {
            if (entry.second && entry.second->client && entry.second->client->hasActiveAudioTrack()) {
                peers.push_back(entry.second);
            }
        }
    }

    for (const auto &peer : peers) {
        if (peer && peer->client) {
            peer->client->sendAudio(packet);
        }
    }
}

std::string AudioRoutePublisher::makePeerKey(const std::string &uuid, const std::string &session) const {
    return uuid + "|" + session;
}

void AudioRoutePublisher::removePeerSession(const std::string &uuid, const std::string &session) {
    std::shared_ptr<PeerSession> peer;
    {
        std::lock_guard<std::mutex> lock(peerSessionsMutex_);
        const auto it = peerSessions_.find(makePeerKey(uuid, session));
        if (it == peerSessions_.end()) {
            return;
        }
        peer = it->second;
        peerSessions_.erase(it);
    }

    if (peer && peer->client) {
        peer->client->shutdown();
    }
}

void AudioRoutePublisher::clearPeerSessions() {
    std::vector<std::shared_ptr<PeerSession>> peers;
    {
        std::lock_guard<std::mutex> lock(peerSessionsMutex_);
        for (auto &entry : peerSessions_) {
            if (entry.second) {
                peers.push_back(entry.second);
            }
        }
        peerSessions_.clear();
    }

    for (const auto &peer : peers) {
        if (peer && peer->client) {
            peer->client->shutdown();
        }
    }
}

void AudioRoutePublisher::setStatus(const std::string &value) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    status_ = value;
}

void AudioRoutePublisher::setError(const std::string &value) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    status_ = value;
    lastError_ = value;
}

void AudioRoutePublisher::setShareLink(const std::string &value) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    shareLink_ = value;
}

void AudioRoutePublisher::disconnectSignaling(bool sendUnpublish) {
    std::lock_guard<std::mutex> lock(signalingMutex_);
    if (sendUnpublish) {
        signaling_.unpublish();
    }
    signaling_.disconnect();
}

bool AudioRoutePublisher::connectAndPublish() {
    const std::string label = startOptions_.label.empty()
        ? router::util::preferredDisplayName(source_)
        : startOptions_.label;
    const std::string publishStreamId = streamId_.empty() ? startOptions_.streamId : streamId_;

    std::lock_guard<std::mutex> lock(signalingMutex_);
    if (!signaling_.connect(startOptions_.server)) {
        setError("Failed to connect to signaling server.");
        return false;
    }

    signaling_.setPassword(startOptions_.password);
    if (router::util::isEncryptionDisabled(startOptions_.password)) {
        signaling_.disableEncryption();
    }

    if (!startOptions_.room.empty()) {
        versus::signaling::RoomConfig roomConfig;
        roomConfig.room = startOptions_.room;
        roomConfig.password = startOptions_.password;
        roomConfig.streamId = publishStreamId;
        roomConfig.label = label;
        roomConfig.salt = startOptions_.salt;
        if (!signaling_.joinRoom(roomConfig)) {
            signaling_.disconnect();
            setError("Failed to join room.");
            return false;
        }
    }

    if (!signaling_.publish(publishStreamId, label)) {
        signaling_.disconnect();
        setError("Failed to publish stream.");
        return false;
    }

    streamId_ = signaling_.getStreamId();
    setShareLink(signaling_.getViewUrl());
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_.clear();
    }
    return true;
}

void AudioRoutePublisher::handleStreamIdCollision(const std::string &message) {
    duplicateStreamId_.store(true, std::memory_order_relaxed);
    reconnecting_.store(false, std::memory_order_relaxed);
    live_.store(false, std::memory_order_relaxed);
    stopSignalingRecoveryThread();
    disconnectSignaling(false);
    clearPeerSessions();
    teardownCapture();
    setShareLink("");
    setError(message.empty() ? "Stream ID is already in use." : message);
}

void AudioRoutePublisher::startSignalingRecovery() {
    if (!live_.load(std::memory_order_relaxed) || reconnecting_.exchange(true) ||
        stopRequested_.load(std::memory_order_relaxed) || duplicateStreamId_.load(std::memory_order_relaxed)) {
        return;
    }

    setStatus("Reconnecting to signaling...");
    stopSignalingRecoveryThread();
    signalingRecoveryThread_ = std::thread([this]() {
        int attempt = 0;
        while (true) {
            if (!live_.load(std::memory_order_relaxed) || stopRequested_.load(std::memory_order_relaxed) ||
                duplicateStreamId_.load(std::memory_order_relaxed)) {
                reconnecting_.store(false, std::memory_order_relaxed);
                return;
            }

            ++attempt;
            spdlog::warn("[AudioRoute] Signaling recovery attempt {} for route {}", attempt, streamId_);
            disconnectSignaling(false);

            if (connectAndPublish()) {
                setStatus("Live");
                reconnecting_.store(false, std::memory_order_relaxed);
                return;
            }

            if (duplicateStreamId_.load(std::memory_order_relaxed)) {
                reconnecting_.store(false, std::memory_order_relaxed);
                return;
            }

            if (attempt == 5 || (attempt % 10) == 0) {
                setStatus("Still reconnecting to signaling...");
            } else {
                setStatus("Reconnecting to signaling...");
            }

            const int delayMs = router::util::reconnectDelayMs(attempt);
            for (int elapsed = 0; elapsed < delayMs; elapsed += 100) {
                if (!live_.load(std::memory_order_relaxed) || stopRequested_.load(std::memory_order_relaxed) ||
                    duplicateStreamId_.load(std::memory_order_relaxed)) {
                    reconnecting_.store(false, std::memory_order_relaxed);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}

void AudioRoutePublisher::stopSignalingRecoveryThread() {
    if (!signalingRecoveryThread_.joinable()) {
        return;
    }
    if (signalingRecoveryThread_.get_id() == std::this_thread::get_id()) {
        signalingRecoveryThread_.detach();
        return;
    }
    signalingRecoveryThread_.join();
}

void AudioRoutePublisher::teardownCapture() {
    captureReady_.store(false, std::memory_order_relaxed);
    audioCapture_.StopCapture();
    opusEncoder_.shutdown();
}

}  // namespace router::app
