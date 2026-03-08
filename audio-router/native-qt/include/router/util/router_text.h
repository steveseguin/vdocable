#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "router/app/source_info.h"

namespace router::util {

std::string trimCopy(const std::string &value);
std::string preferredDisplayName(const router::app::SourceInfo &source);
std::string sanitizeRouteId(const std::string &value, std::size_t maxLength);
std::string makeDefaultStreamId(const router::app::SourceInfo &source, uint64_t suffixSeed);
std::string formatSourceLabel(const router::app::SourceInfo &source);
bool isEncryptionDisabled(const std::string &password);
std::string effectivePublishPassword(const std::string &password);
std::string effectivePublishedStreamId(const std::string &streamId, const std::string &password, const std::string &salt);
bool isStreamIdInUseAlert(const std::string &message);
int reconnectDelayMs(int attempt);

}  // namespace router::util
