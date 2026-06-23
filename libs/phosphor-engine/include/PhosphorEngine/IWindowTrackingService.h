// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <phosphorengine_export.h>

#include <QHash>
#include <QList>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <optional>

class QObject;

namespace PhosphorZones {
class Layout;
}

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorEngine {

class PHOSPHORENGINE_EXPORT IWindowTrackingService
{
public:
    virtual ~IWindowTrackingService() = default;

    virtual QObject* asQObject() = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen manager access
    // Tech debt: returns concrete ScreenManager* — should be an IScreenManager
    // interface once one exists. Acceptable as a stepping stone; the
    // IScreenManager extraction is tracked separately.
    // ═══════════════════════════════════════════════════════════════════════════

    virtual PhosphorScreens::ScreenManager* screenManager() const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone assignment management
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                                    int virtualDesktop) = 0;
    virtual void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                     int virtualDesktop) = 0;
    virtual void unassignWindow(const QString& windowId) = 0;

    virtual const QHash<QString, QStringList>& zoneAssignments() const = 0;

    /// A window's recorded snap zone(s), preferring the LIVE assignment but falling
    /// back to the DURABLE placement-record snap slot when the live cache is cold.
    /// The live `zoneAssignments()` map is runtime-only — a daemon restart (and
    /// `handoffRelease` on autotile entry) clears it — so consumers that must
    /// survive a restart (the autotile→snap resnap) read this instead. Returns an
    /// empty list for a window that was never snapped in either source.
    virtual QStringList recordedSnapZones(const QString& windowId) const = 0;

    virtual const QHash<QString, QString>& screenAssignments() const = 0;
    virtual QString zoneForWindow(const QString& windowId) const = 0;
    virtual QStringList zonesForWindow(const QString& windowId) const = 0;
    /// The screen a window is assigned to (empty when none), canonicalizing the
    /// id — the point accessor callers use instead of screenAssignments().value()
    /// so a window resolves across the effect-restart re-identification skew (#628).
    virtual QString screenForWindow(const QString& windowId) const = 0;
    /// Same, returning @p defaultScreen when the window has no assignment.
    virtual QString screenForWindow(const QString& windowId, const QString& defaultScreen) const = 0;
    virtual QStringList windowsInZone(const QString& zoneId) const = 0;
    virtual bool isWindowSnapped(const QString& windowId) const = 0;
    virtual QString findEmptyZone(const QString& screenId = QString()) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-snap
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void recordSnapIntent(const QString& windowId, bool wasUserInitiated) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating state
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool isWindowFloating(const QString& windowId) const = 0;
    virtual void setWindowFloating(const QString& windowId, bool floating) = 0;
    virtual void unsnapForFloat(const QString& windowId) = 0;
    virtual bool clearFloatingForSnap(const QString& windowId) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Sticky state
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool isWindowSticky(const QString& windowId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Pre-float zone/screen restore
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QStringList preFloatZones(const QString& windowId) const = 0;
    virtual QString preFloatScreen(const QString& windowId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-snap / pending restore
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool clearAutoSnapped(const QString& windowId) = 0;
    virtual bool consumePendingAssignment(const QString& windowId) = 0;
    virtual const QHash<QString, QList<PendingRestore>>& pendingRestoreQueues() const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Last-used zone tracking
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void updateLastUsedZone(const QString& zoneId, const QString& screenId, const QString& windowClass,
                                    int virtualDesktop) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // App identity
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QString currentAppIdFor(const QString& anyWindowId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Geometry resolution
    // ═══════════════════════════════════════════════════════════════════════════

    virtual std::optional<QRect> validatedUnmanagedGeometry(const QString& windowId, const QString& screenId,
                                                            bool exactOnly = false) const = 0;

    /// Record a window's SHARED free/float geometry (the single float-back store —
    /// the placement record's freeGeometryByScreen). This is the ONE writer all
    /// float-back captures route through (effect pre-tile/pre-snap capture, drag
    /// store, float-toggle capture), so snap and autotile read the same value and
    /// never drift. @p overwrite=false leaves an existing entry for @p screenId
    /// untouched (first-capture-wins). No-op on an invalid geometry.
    virtual void recordFreeGeometry(const QString& windowId, const QString& screenId, const QRect& geometry,
                                    bool overwrite) = 0;

    /// Clear a window's shared free/float geometry (all screens) from the record,
    /// leaving its engine slots intact. For the drag-out / layout-change consume
    /// paths that restore the float-back once and must not re-apply it.
    virtual void clearFreeGeometry(const QString& windowId) = 0;
    virtual QRect zoneGeometry(const QString& zoneId, const QString& screenId = QString()) const = 0;
    virtual QRect resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const = 0;
    virtual QString resolveEffectiveScreenId(const QString& screenId) const = 0;

    /// The unified, engine-agnostic placement store. Engines reach it through the
    /// tracking service to capture/restore the one WindowPlacement per window.
    virtual WindowPlacementStore& placementStore() = 0;
    // Tech debt: takes concrete Layout* — should take an opaque layout identifier
    // once the layout interface is extracted. Acceptable for this extraction phase.
    virtual QString findEmptyZoneInLayout(PhosphorZones::Layout* layout, const QString& screenId,
                                          int desktopFilter = 0) const = 0;
    virtual QSet<QUuid> buildOccupiedZoneSet(const QString& screenFilter = QString(), int desktopFilter = 0) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Resnap buffer
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QVector<ResnapEntry> takeResnapBuffer() = 0;
};

} // namespace PhosphorEngine
