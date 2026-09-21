// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorEngine/IOverviewModelSource.h>
#include <PhosphorWorkspaces/WorkspaceMap.h>

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/// The overview's placement-mode vocabulary on the wire.
namespace OverviewMode {
inline constexpr QLatin1String Snapping{"snapping"};
inline constexpr QLatin1String Tiling{"tiling"};
inline constexpr QLatin1String Scrolling{"scrolling"};
inline constexpr QLatin1String None{"none"};
}

/// One window the daemon tracks, as the builder needs it: where it sits and
/// what it is. The controller fills these from the WindowRegistry plus the
/// daemon's window-to-screen resolver; the tests fill them by hand.
struct OverviewTrackedWindow
{
    QString id;
    /// Physical screen id (canonical), or empty when unknown.
    QString screenId;
    /// 1-based desktop, 0 for sticky / unknown.
    int desktop = 0;
    bool sticky = false;
    bool minimized = false;
    /// Frame rect in global logical pixels, or null when unknown.
    QRect frame;
    /// True for the types the overview omits (docks, desktops,
    /// notifications and their kin).
    bool omitted = false;
};

/// Builds the overview model JSON from the workspace map, the engines'
/// per-key read surfaces and the daemon's window tracking. Every external
/// fact arrives through a provider so the builder is a pure function of its
/// inputs and the daemon test can drive it with fakes.
///
/// Wire schema (v 1) is documented in dbus/org.plasmazones.Overview.xml.
/// Rects are workspace-local logical pixels: the engine's global rect minus
/// the owning physical output's origin. Every tracked, non-omitted window on
/// a (screen, desktop) the map owns appears exactly once; sticky windows
/// appear once, on their screen's current workspace, flagged sticky.
class OverviewModelBuilder
{
public:
    struct Inputs
    {
        const PhosphorWorkspaces::WorkspaceMap* map = nullptr;
        QString activity;
        /// Current desktop (1-based) per physical screen; 0 when unknown.
        QHash<QString, int> currentDesktopByScreen;
        /// desktopId → live 1-based global index (0 when unknown).
        std::function<int(const QString& desktopId)> desktopIndexOf;
        /// Physical screen id → logical geometry (null when unknown).
        std::function<QRect(const QString& screenId)> screenGeometry;
        /// Physical screen id → its virtual-screen ids (empty when the output
        /// is not subdivided; the builder then queries the physical id).
        std::function<QStringList(const QString& screenId)> virtualScreensFor;
        /// Placement mode for a (physical screen, desktop) under the activity.
        std::function<QLatin1String(const QString& screenId, int desktop)> modeFor;
        /// Engine read surface per mode; null when that engine is absent.
        PhosphorEngine::IOverviewModelSource* snapping = nullptr;
        PhosphorEngine::IOverviewModelSource* tiling = nullptr;
        PhosphorEngine::IOverviewModelSource* scrolling = nullptr;
        /// Every window the daemon tracks.
        QList<OverviewTrackedWindow> windows;
    };

    /// Build the model WITHOUT the generation stamps (the controller compares
    /// this against the last published payload, then stamps).
    static QJsonObject build(const Inputs& in);

    /// The workspace-local rect for an engine-space rect on the output at
    /// @p outputGeometry.
    static QRect toWorkspaceLocal(const QRect& engineRect, const QRect& outputGeometry);
};

} // namespace PlasmaZones
