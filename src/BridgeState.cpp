#include "BridgeState.h"
namespace bridge {
State &state() { static State instance; return instance; }

void refreshProviderReadyState() {
    auto &s = state();
    const bool ready = s.providerInputConnected.load()
        && s.providerOutputConnected.load()
        && s.helloReceived.load()
        && s.cameraValid.load();

    const bool wasReady = s.providerReady.exchange(ready);
    if (ready && !wasReady) {
        // This rising edge is the universal equivalent of starting the old Lua
        // bridge: provider + camera are now present, so IGCS may connect safely.
        s.igcsHandshakePending = true;
        s.igcsConnected = false;
    } else if (!ready && wasReady) {
        s.igcsConnected = false;
        s.sessionActive = false;
    }
}

void refreshProviderConnectedState() {
    auto &s = state();
    s.providerConnected = s.providerInputConnected.load() && s.providerOutputConnected.load();
    refreshProviderReadyState();
}
}
