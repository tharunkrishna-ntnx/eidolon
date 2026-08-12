#pragma once

#include <cstdint>
#include <string>

namespace eidolon {

// Well-known TCP ports (defaults matching eidolon.conf).
// Services read the actual values from their gflags at startup.
constexpr uint16_t kDefaultStyxPort      = 7101;
constexpr uint16_t kDefaultPrometheusPort = 7102;
constexpr uint16_t kDefaultMnemosynePort = 7200;
constexpr uint16_t kDefaultHermesPort    = 7300;
constexpr uint16_t kDefaultDiscoveryPort = 7100;

// Simple length-prefixed wire format shared by all TCP services.
//   [4-byte big-endian payload length][payload bytes]
// Payload is a plain-text command string. Responses use the same framing.

constexpr uint32_t kMaxPayloadBytes = 16 * 1024 * 1024;  // 16 MiB

// Encode a 4-byte big-endian length prefix + payload into a string buffer.
std::string FrameMessage(const std::string& payload);

// Try to extract one complete framed message from |buf|.
// On success, writes the payload to |out| and erases the consumed bytes
// from |buf|, returning true.  Returns false if the buffer does not yet
// contain a complete frame.
bool ExtractFrame(std::string& buf, std::string& out);

// --- Mnemosyne text protocol commands ---
// GET <key>\n            -> VALUE <key> <len>\n<bytes>\n | NOT_FOUND\n
// PUT <key> <len>\n<bytes>\n  -> OK\n | ERROR <msg>\n
// DELETE <key>\n         -> OK\n | NOT_FOUND\n
// SCAN <prefix>\n        -> ENTRIES <count>\n(<key> <len>\n<bytes>\n)*
// STATS\n                -> STATS_REPLY <json>\n

// --- Hermes text protocol commands ---
// WRITE <block_id> <len>\n<bytes>\n  -> OK\n | ERROR <msg>\n
// READ <block_id>\n                  -> DATA <block_id> <len>\n<bytes>\n | NOT_FOUND\n
// DELETE <block_id>\n                -> OK\n | NOT_FOUND\n
// REPLICATE <block_id> <target_ip> <target_port>\n -> OK\n | ERROR <msg>\n
// STATS\n                            -> STATS_REPLY <json>\n

// --- Styx text protocol commands ---
// REG <service> <ip> <port>\n   -> OK\n
// UNREG <service> <ip>\n        -> OK\n
// LOOKUP <service>\n            -> ENDPOINTS <count>\n(<ip>:<port>\n)*
// MEMBERS\n                     -> MEMBERS_LIST <count>\n(<ip> <state>\n)*
// KGET <key>\n                  -> VALUE <len>\n<bytes>\n | NOT_FOUND\n
// KPUT <key> <len>\n<bytes>\n   -> OK\n | ERROR <msg>\n
// LEADER\n                      -> LEADER_IS <ip>:<port>\n

}  // namespace eidolon
