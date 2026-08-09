#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#include <windows.h>
#include <imgui.h>
#include <reshade.hpp>
#include "BridgeState.h"
#include "NamedPipeServer.h"
#include "IgcsConnectorLink.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <string>

extern "C" const char *NAME = "IGCSDOF Universal Bridge";
extern "C" const char *DESCRIPTION = "Universal queued Named Pipe camera-provider bridge for IGCS Connector / IGCSDOF.";

namespace {
HMODULE g_selfModule = nullptr;

bool hasSelfExport(const char *name) {
    return g_selfModule != nullptr && GetProcAddress(g_selfModule, name) != nullptr;
}

void tableLine(const char *label, const char *value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
}

void tableStatus(
    const char *label,
    bool ok,
    const char *okText,
    const char *badText
) {
    tableLine(label, ok ? okText : badText);
}

float calculateLabelColumnWidth() {
    static constexpr std::array<const char *, 14> kLabels = {
        "Provider",
        "Engine",
        "Camera mode",
        "Status",
        "Provider status",
        "Provider ready",
        "Camera channel",
        "Command channel",
        "Camera data",
        "IGCS Connector",
        "IGCS command exports",
        "Session",
        "Protocol / Bridge",
        "Position"
    };

    float longestLabel = 0.0f;
    for (const char *label : kLabels) {
        longestLabel = std::max(
            longestLabel,
            ImGui::CalcTextSize(label).x
        );
    }

    const ImGuiStyle &style = ImGui::GetStyle();
    return longestLabel +
        (style.CellPadding.x * 2.0f) +
        style.ItemSpacing.x;
}

void setupStatusColumns(float labelWidth) {
    ImGui::TableSetupColumn(
        "Label",
        ImGuiTableColumnFlags_WidthFixed,
        labelWidth
    );
    ImGui::TableSetupColumn(
        "Value",
        ImGuiTableColumnFlags_WidthStretch
    );
}

void displaySettings(reshade::api::effect_runtime *) {
    auto &s = bridge::state();

    std::string game;
    std::string provider;
    std::string engine;
    std::string version;
    std::string error;
    bridge::EngineProfile engineProfile = bridge::EngineProfile::Normalized;
    bridge::CameraInputMode cameraInputMode = bridge::CameraInputMode::NormalizedBasis;
    CameraToolsData cameraCopy{};

    {
        std::scoped_lock lock(s.mutex);
        game = s.game;
        provider = s.provider;
        engine = s.engine;
        version = s.engineVersion;
        error = s.lastError;
        engineProfile = s.engineProfile;
        cameraInputMode = s.cameraInputMode;
        cameraCopy = s.camera;
    }

    const float labelColumnWidth = calculateLabelColumnWidth();

    ImGui::TextUnformatted("IGCSDOF Universal Bridge");
    ImGui::Separator();

    if (ImGui::BeginTable(
            "IGCSDOFBridgeIdentity",
            2,
            ImGuiTableFlags_SizingStretchProp
        )) {
        setupStatusColumns(labelColumnWidth);
        tableLine(
            "Provider",
            provider.empty() ? "Not connected" : provider.c_str()
        );

        const std::string engineDisplay =
            engine.empty()
                ? "Not available"
                : engine + (version.empty() ? "" : " " + version);
        tableLine("Engine", bridge::engineProfileDisplayName(s.engineProfile));
        tableLine(
            "Camera mode",
            cameraInputMode == bridge::CameraInputMode::RawEuler
                ? "Raw engine values"
                : "Provider normalized"
        );

        ImGui::EndTable();
    }

    ImGui::SeparatorText("Connection");

    if (ImGui::BeginTable(
            "IGCSDOFBridgeConnection",
            2,
            ImGuiTableFlags_SizingStretchProp
        )) {
        setupStatusColumns(labelColumnWidth);

        const bool globalOk = s.providerReady && s.igcsConnected;
        tableStatus(
            "Status",
            globalOk,
            "OK",
            s.providerConnected ? "NOK" : "Waiting for provider"
        );
        tableStatus(
            "Provider status",
            s.providerConnected,
            "Connected",
            "Waiting for both channels"
        );
        tableStatus(
            "Provider ready",
            s.providerReady,
            "Yes",
            "Waiting for HELLO/camera"
        );
        tableStatus(
            "Camera channel",
            s.providerInputConnected,
            "Connected",
            "Disconnected"
        );
        tableStatus(
            "Command channel",
            s.providerOutputConnected,
            "Connected",
            "Disconnected"
        );
        tableStatus(
            "Camera data",
            s.cameraValid,
            "Valid",
            "Unavailable"
        );
        tableStatus(
            "IGCS Connector",
            s.igcsConnected,
            "Connected",
            "Not found"
        );

        const bool exportStart =
            hasSelfExport("IGCS_StartScreenshotSession");
        const bool exportEnd =
            hasSelfExport("IGCS_EndScreenshotSession");
        const bool exportMove =
            hasSelfExport("IGCS_MoveCameraMultishot");
        const bool exportPano =
            hasSelfExport("IGCS_MoveCameraPanorama");
        const bool allExports =
            exportStart && exportEnd && exportMove && exportPano;

        tableStatus(
            "IGCS command exports",
            allExports,
            "4 / 4 visible",
            "Missing export"
        );

        ImGui::EndTable();
    }

    ImGui::SeparatorText("State");

    if (ImGui::BeginTable(
            "IGCSDOFBridgeState",
            2,
            ImGuiTableFlags_SizingStretchProp
        )) {
        setupStatusColumns(labelColumnWidth);

        tableLine(
            "Session",
            s.sessionActive ? "Rendering" : "Idle"
        );
        tableLine("Protocol / Bridge", "v1 / 0.6.7");

        if (s.cameraValid) {
            char position[128]{};
            std::snprintf(
                position,
                sizeof(position),
                "%.3f   %.3f   %.3f",
                cameraCopy.coordinates.values[0],
                cameraCopy.coordinates.values[1],
                cameraCopy.coordinates.values[2]
            );
            tableLine("Position", position);

            char fov[32]{};
            std::snprintf(
                fov,
                sizeof(fov),
                "%.2f",
                cameraCopy.fov
            );
            tableLine("FOV", fov);
        }

        ImGui::EndTable();
    }

    if (!error.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Last error: %s", error.c_str());
    }

}

void onPresent(reshade::api::effect_runtime *) { bridge::publishCameraData(); }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = module;
        DisableThreadLibraryCalls(module);
        if (!reshade::register_addon(module)) return FALSE;
        reshade::register_event<reshade::addon_event::reshade_present>(onPresent);
        reshade::register_overlay(nullptr, displaySettings);
        bridge::startPipeServer();
    } else if (reason == DLL_PROCESS_DETACH) {
        bridge::stopPipeServer();
        reshade::unregister_event<reshade::addon_event::reshade_present>(onPresent);
        reshade::unregister_overlay(nullptr, displaySettings);
        reshade::unregister_addon(module);
    }
    return TRUE;
}
