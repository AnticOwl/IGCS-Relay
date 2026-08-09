#pragma once
#include <string>

namespace bridge {
void startPipeServer();
void stopPipeServer();

// Thread-safe producer API. Only the dedicated output worker touches the
// BridgeToProvider pipe handle.
bool sendLine(const std::string &line);
}
