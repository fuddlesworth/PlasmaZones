// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/EngineTypes.h>
#include <phosphorengineapi_export.h>

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

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PhosphorEngineApi {

class PHOSPHORENGINEAPI_EXPORT IWindowTrackingService
{
public:
    virtual ~IWindowTrackingService() = default;

    virtual QObject* asQObject() = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen manager access
    // Tech debt: returns concrete ScreenManager* — should be an IScreenManager
    // interface once one exists. Acceptable for now as a stepping stone.
    // ═══════════════════════════════════════════════════════════════════════════

    virtual Phosphor::Screens::ScreenManager* screenManager() const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone assignment management
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                                    int virtualDesktop) = 0;
    virtual void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                     int virtualDesktop) = 0;
    virtual void unassignWindow(const QString& windowId) = 0;

    virtual const QHash<QString, QStringList>& zoneAssignments() const = 0;
    virtual const QHash<QString, QString>& screenAssignments() const = 0;
    virtual QString zoneForWindow(const QString& windowId) const = 0;
    virtual QStringList zonesForWindow(const QString& windowId) const = 0;
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
    virtual QRect zoneGeometry(const QString& zoneId, const QString& screenId = QString()) const = 0;
    virtual QRect resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const = 0;
    virtual QString resolveEffectiveScreenId(const QString& screenId) const = 0;
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

} // namespace PhosphorEngineApi
