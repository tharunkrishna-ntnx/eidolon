#pragma once

#include <cstdint>
#include <string>

namespace eidolon {
namespace hermes {

// Send a block to a remote Hermes instance for replication.
bool ReplicateBlock(const std::string& target_ip, uint16_t target_port,
                    const std::string& block_id, const std::string& data);

}  // namespace hermes
}  // namespace eidolon
