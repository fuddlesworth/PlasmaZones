// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

/**
 * @brief Single source of truth for the animation event tree hierarchy.
 *
 * Shared between src/core/animationprofile.cpp (daemon) and
 * kwin-effect/plasmazoneseffect.cpp (compositor plugin) via header-only include.
 *
 * "global" is the root and has no entry here (no parent).
 *
 * Tree structure:
 *   global
 *     +-- windowGeometry
 *     |   +-- snap -> snapIn, snapOut, snapResize
 *     |   +-- layoutSwitch -> layoutSwitchIn, layoutSwitchOut
 *     |   +-- autotileBorder -> borderIn, borderOut
 *     +-- overlay
 *     |   +-- zoneHighlight -> zoneHighlightIn, zoneHighlightOut
 *     |   +-- osd -> layoutOsdIn, layoutOsdOut, navigationOsdIn, navigationOsdOut
 *     |   +-- popup -> layoutPickerIn, layoutPickerOut, snapAssistIn, snapAssistOut
 *     |   +-- zoneSelector -> zoneSelectorIn, zoneSelectorOut
 *     |   +-- preview -> previewIn, previewOut
 *     +-- dim
 */

/// Valid geometry mode strings for animation shader effects.
/// Used by AnimationShaderRegistry and AnimationProfile deserialization.
static constexpr const char* ValidGeometryModes[] = {"morph", "popin", "slidefade"};
static constexpr int ValidGeometryModeCount = sizeof(ValidGeometryModes) / sizeof(ValidGeometryModes[0]);

struct AnimationTreeEdge
{
    const char* child;
    const char* parent;
};

// clang-format off
static constexpr AnimationTreeEdge AnimationTreeEdges[] = {
    // Domain nodes → global
    {"windowGeometry", "global"},
    {"overlay", "global"},
    {"dim", "global"},
    // Window geometry categories
    {"snap", "windowGeometry"},
    {"snapIn", "snap"},
    {"snapOut", "snap"},
    {"snapResize", "snap"},
    {"layoutSwitch", "windowGeometry"},
    {"layoutSwitchIn", "layoutSwitch"},
    {"layoutSwitchOut", "layoutSwitch"},
    {"autotileBorder", "windowGeometry"},
    {"borderIn", "autotileBorder"},
    {"borderOut", "autotileBorder"},
    // Overlay categories
    {"zoneHighlight", "overlay"},
    {"zoneHighlightIn", "zoneHighlight"},
    {"zoneHighlightOut", "zoneHighlight"},
    {"osd", "overlay"},
    {"layoutOsdIn", "osd"},
    {"layoutOsdOut", "osd"},
    {"navigationOsdIn", "osd"},
    {"navigationOsdOut", "osd"},
    {"popup", "overlay"},
    {"layoutPickerIn", "popup"},
    {"layoutPickerOut", "popup"},
    {"snapAssistIn", "popup"},
    {"snapAssistOut", "popup"},
    {"zoneSelector", "overlay"},
    {"zoneSelectorIn", "zoneSelector"},
    {"zoneSelectorOut", "zoneSelector"},
    {"preview", "overlay"},
    {"previewIn", "preview"},
    {"previewOut", "preview"},
};
// clang-format on

static constexpr int AnimationTreeEdgeCount = sizeof(AnimationTreeEdges) / sizeof(AnimationTreeEdges[0]);

} // namespace PlasmaZones
