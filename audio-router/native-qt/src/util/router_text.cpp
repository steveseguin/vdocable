#include "router/util/router_text.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include <mbedtls/md.h>

namespace router::util {
namespace {

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string baseName(std::string value) {
    const std::size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) {
        value = value.substr(slash + 1);
    }
    const std::size_t dot = value.find_last_of('.');
    if (dot != std::string::npos) {
        value = value.substr(0, dot);
    }
    return value;
}

std::vector<uint8_t> sha256Bytes(const std::string &input) {
    std::vector<uint8_t> digest(32, 0);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        return digest;
    }

    if (mbedtls_md_setup(&ctx, info, 0) != 0) {
        mbedtls_md_free(&ctx);
        return digest;
    }

    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, reinterpret_cast<const unsigned char *>(input.data()), input.size());
    mbedtls_md_finish(&ctx, digest.data());
    mbedtls_md_free(&ctx);
    return digest;
}

std::string toHex(const std::vector<uint8_t> &bytes, std::size_t maxChars) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(std::min(maxChars, bytes.size() * 2));
    for (uint8_t byte : bytes) {
        if (out.size() + 2 > maxChars) {
            break;
        }
        out.push_back(kHex[(byte >> 4) & 0x0F]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

}  // namespace

std::string trimCopy(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string preferredDisplayName(const router::app::SourceInfo &source) {
    const std::string displayName = trimCopy(source.displayName);
    if (!displayName.empty()) {
        return displayName;
    }
    const std::string executableName = trimCopy(source.executableName);
    if (!executableName.empty()) {
        return executableName;
    }
    return "Process " + std::to_string(source.processId);
}

std::string sanitizeRouteId(const std::string &value, std::size_t maxLength) {
    std::string sanitized = trimCopy(value);
    for (char &ch : sanitized) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
            ch = '_';
        }
    }
    if (sanitized.size() > maxLength) {
        sanitized.resize(maxLength);
    }
    return sanitized;
}

std::string makeDefaultStreamId(const router::app::SourceInfo &source, uint64_t suffixSeed) {
    std::string base = trimCopy(source.executableName);
    if (base.empty()) {
        base = trimCopy(source.displayName);
    }
    if (base.empty()) {
        base = "audio";
    }
    base = baseName(base);
    base = sanitizeRouteId(lowerCopy(base), 24);
    if (base.empty()) {
        base = "audio";
    }
    return base + "_" + std::to_string(suffixSeed % 1000000ULL);
}

std::string formatSourceLabel(const router::app::SourceInfo &source) {
    std::string label = preferredDisplayName(source);
    const std::string executableName = trimCopy(source.executableName);
    const std::string displayName = trimCopy(source.displayName);
    if (!executableName.empty() && executableName != displayName) {
        label += " [" + executableName + "]";
    }
    label += " (PID " + std::to_string(source.processId) + ")";
    return label;
}

bool isEncryptionDisabled(const std::string &password) {
    const std::string lower = lowerCopy(trimCopy(password));
    return lower == "false" || lower == "0" || lower == "off";
}

std::string effectivePublishPassword(const std::string &password) {
    if (isEncryptionDisabled(password)) {
        return "";
    }
    const std::string trimmed = trimCopy(password);
    return trimmed.empty() ? "someEncryptionKey123" : trimmed;
}

std::string effectivePublishedStreamId(const std::string &streamId, const std::string &password, const std::string &salt) {
    const std::string sanitized = sanitizeRouteId(streamId, 64);
    if (sanitized.empty()) {
        return "";
    }

    if (isEncryptionDisabled(password)) {
        return sanitized;
    }

    const std::string effectiveSalt = trimCopy(salt).empty() ? "vdo.ninja" : trimCopy(salt);
    const std::string pass = effectivePublishPassword(password);
    const std::string hashSuffix = toHex(sha256Bytes(pass + effectiveSalt), 6);
    return sanitized + hashSuffix;
}

bool isStreamIdInUseAlert(const std::string &message) {
    const std::string lower = lowerCopy(message);
    return lower.find("streamid-already-published") != std::string::npos ||
           lower.find("already in use") != std::string::npos ||
           lower.find("already has this stream id") != std::string::npos ||
           lower.find("already has this streamid") != std::string::npos ||
           lower.find("duplicate stream") != std::string::npos ||
           ((lower.find("stream") != std::string::npos ||
             lower.find("stream id") != std::string::npos ||
             lower.find("streamid") != std::string::npos) &&
            (lower.find("in use") != std::string::npos ||
             lower.find("already has") != std::string::npos));
}

int reconnectDelayMs(int attempt) {
    if (attempt <= 1) {
        return 2000;
    }
    int delayMs = 2000;
    for (int i = 1; i < attempt; ++i) {
        delayMs = std::min(delayMs * 2, 15000);
    }
    return delayMs;
}

}  // namespace router::util
