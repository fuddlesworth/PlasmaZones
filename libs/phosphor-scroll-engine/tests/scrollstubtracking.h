// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// A headless IWindowTrackingService for the three engine paths that are
// unreachable without one: the STICKY insertion gate (which asks the tracker
// whether a window is sticky) and the CLIENT-DECIDES default width and height
// (which open a column, and size a tile, from the tracker's reported
// unmanaged geometry). All are per-screen rule slots, so without this stub
// their per-screen resolution has no observable at all — the engine
// short-circuits on the null tracker before the resolved value is ever used.
//
// Everything else answers the empty/false/no-op value. The placement store is
// a real (empty) WindowPlacementStore: the open path consults it for a
// float-reopen record, and an empty store is exactly the "no record" case
// these suites want.

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <optional>

namespace ScrollTestUtils {

class StubWindowTracking : public QObject, public PhosphorEngine::IWindowTrackingService
{
public:
    using QObject::QObject;

    /// Windows the sticky gate must see as sticky.
    QSet<QString> stickyWindows;
    /// What validatedUnmanagedGeometry answers — the client's own size. The
    /// ClientDecides width opens the column at its MAIN extent and the
    /// ClientDecides height sizes the tile from its CROSS extent. Invalid
    /// means "no size on record", the case that falls back to the resolved
    /// default.
    QRect unmanagedGeometry;
    /// Counts validatedUnmanagedGeometry calls. The per-window contract a
    /// sizing intent needs is now structural — the resolver has no appId
    /// fallback left to opt out of — so what is worth observing here is that
    /// the sizing arms consult the record at all.
    mutable int unmanagedGeometryCalls = 0;

    QObject* asQObject() override
    {
        return this;
    }
    PhosphorScreens::ScreenManager* screenManager() const override
    {
        return nullptr;
    }

    void assignWindowToZone(const QString&, const QString&, const QString&, int) override
    {
    }
    void assignWindowToZones(const QString&, const QStringList&, const QString&, int) override
    {
    }
    void unassignWindow(const QString&) override
    {
    }
    QStringList recordedSnapZones(const QString&) const override
    {
        return {};
    }
    QString zoneForWindow(const QString&) const override
    {
        return {};
    }
    QStringList zonesForWindow(const QString&) const override
    {
        return {};
    }
    QString screenForWindow(const QString&) const override
    {
        return {};
    }
    QString screenForWindow(const QString&, const QString& defaultScreen) const override
    {
        return defaultScreen;
    }
    QStringList windowsInZone(const QString&) const override
    {
        return {};
    }
    bool isWindowSnapped(const QString&) const override
    {
        return false;
    }
    QString findEmptyZone(const QString&) const override
    {
        return {};
    }
    void recordSnapIntent(const QString&, bool) override
    {
    }
    bool isWindowFloating(const QString&) const override
    {
        return false;
    }
    void setWindowFloating(const QString&, bool) override
    {
    }
    void unsnapForFloat(const QString&) override
    {
    }
    bool clearFloatingForSnap(const QString&) override
    {
        return false;
    }
    bool isWindowSticky(const QString& windowId) const override
    {
        return stickyWindows.contains(windowId);
    }
    QStringList preFloatZones(const QString&) const override
    {
        return {};
    }
    QString preFloatScreen(const QString&) const override
    {
        return {};
    }
    void clearPreFloatZone(const QString&) override
    {
    }
    bool clearAutoSnapped(const QString&) override
    {
        return false;
    }
    bool consumePendingAssignment(const QString&) override
    {
        return false;
    }
    const QHash<QString, QList<PhosphorEngine::PendingRestore>>& pendingRestoreQueues() const override
    {
        return m_pendingRestores;
    }
    void updateLastUsedZone(const QString&, const QString&, const QString&, int) override
    {
    }
    QString currentAppIdFor(const QString& anyWindowId) const override
    {
        // The suite's ids are "app|instance"; the engine's own canonical
        // parse would answer the same, and the record lookups it feeds run
        // against an empty store either way.
        const int sep = anyWindowId.indexOf(QLatin1Char('|'));
        return sep > 0 ? anyWindowId.left(sep) : QString();
    }
    /// Stub: the fake has no screen manager, so it fails OPEN exactly as the
    /// real service does in that case. Tests that need the refusal drive it
    /// through a FakeScreenProvider-backed service instead.
    bool geometryBelongsToScreen(const QRect&, const QString&) const override
    {
        return true;
    }
    std::optional<QRect> validatedUnmanagedGeometry(const QString&, const QString&) const override
    {
        ++unmanagedGeometryCalls;
        if (!unmanagedGeometry.isValid()) {
            return std::nullopt;
        }
        return unmanagedGeometry;
    }
    void recordFreeGeometry(const QString&, const QString&, const QRect&, bool) override
    {
    }
    /// The screen-scoped overload keeps the interface's default (it forwards
    /// here); the using-declaration stops this override from hiding it.
    using PhosphorEngine::IWindowTrackingService::clearFreeGeometry;
    void clearFreeGeometry(const QString&) override
    {
    }
    QRect zoneGeometry(const QString&, const QString&) const override
    {
        return {};
    }
    QRect resolveZoneGeometry(const QStringList&, const QString&) const override
    {
        return {};
    }
    QString resolveEffectiveScreenId(const QString& screenId) const override
    {
        return screenId;
    }
    PhosphorEngine::WindowPlacementStore& placementStore() override
    {
        return m_store;
    }
    QString findEmptyZoneInLayout(PhosphorZones::Layout*, const QString&, int) const override
    {
        return {};
    }
    QSet<QUuid> buildOccupiedZoneSet(const QString&, int) const override
    {
        return {};
    }
    QVector<PhosphorEngine::ResnapEntry> takeResnapBuffer() override
    {
        return {};
    }

private:
    QHash<QString, QList<PhosphorEngine::PendingRestore>> m_pendingRestores;
    PhosphorEngine::WindowPlacementStore m_store;
};

} // namespace ScrollTestUtils
