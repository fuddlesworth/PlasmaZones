// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forward declarations (and the few must-be-complete includes threaded
// between them) for daemon.h — split out so the class header itself stays
// within the file-size policy. Every entity here is consumed by daemon.h
// as a pointer/reference member or a signature type.

namespace PhosphorScreens {
class PlasmaPanelSource;
class DBusScreenAdaptor;
}

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/QtQuickClockManager.h>
#include <PhosphorConfig/IBackend.h>

namespace PhosphorAnimation {
class CurveLoader;
class ProfileLoader;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PhosphorEngine {
class WindowRegistry;
}

namespace PhosphorWorkspaces {
class ActivityManager;
class VirtualDesktopManager;
}

namespace PhosphorServiceIdle {
class IdleService;
}

// PhosphorRules::RuleSet is held as a value member below
// (m_excludeRuleSet) — needs a complete type, so include the header
// rather than forward-declare. RuleStore stays in the header by
// pointer only; including RuleSet.h leaves the store forward
// declared here.
#include <PhosphorRules/RuleSet.h>

namespace PhosphorRules {
class RuleStore;
}

namespace PhosphorZones {
class Layout;
class LayoutComputeService;
class LayoutRegistry;
class ScrollingTemplate;
class ScrollingTemplateStore;
class ZoneDetector;
} // namespace PhosphorZones

// `AssignmentEntry::Mode` appears in member-function signatures below, so
// the full struct definition must be visible here (a forward declaration
// can't surface a nested enum). The header is LGPL-LGPL safe (PhosphorZones
// to daemon header is the standard direction).
#include <PhosphorZones/AssignmentEntry.h>

namespace PlasmaZones {

enum class DisabledReason;
class Settings;
class OverlayService;

class ShortcutManager;
class LayoutAdaptor;
class SettingsAdaptor;
class ShaderAdaptor;
class ControlAdaptor;
class CompositorBridgeAdaptor;
class OverlayAdaptor;
class OverviewAdaptor;
class OverviewController;
class ZoneDetectionAdaptor;
class WindowTrackingAdaptor;
class WindowDragAdaptor;
class RuleAdaptor;
class ZoneSelectorController;
class UnifiedLayoutController;
class WorkspaceController;
class TilingAdaptor;
class AutotileAdaptor;
class ScrollingAdaptor;
class ScreenModeRouter;
class CrossSurfaceResolver;
class DaemonScreenModeAdapter;
class DaemonSettingsGateAdapter;
class DaemonWorkspaceStateAdapter;

} // namespace PlasmaZones

namespace PhosphorContext {
class ContextResolver;
} // namespace PhosphorContext

namespace PlasmaZones {
class SettingsConfigStore;
class SnapAdaptor;
class ShaderRegistry;
} // namespace PlasmaZones

namespace PhosphorTiles {
class AlgorithmRegistry;
class ScriptedAlgorithmLoader;
}
