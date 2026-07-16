// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorTileEngine/IAutotileSettings.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>

#include <QObject>

namespace PlasmaZones::TestHelpers {

/**
 * @brief Minimal IAutotileSettings for AutotileEngine tests.
 *
 * AutotileEngine reaches its settings through
 * qobject_cast<PhosphorEngine::IAutotileSettings*>(engineSettings()), so a stub
 * has to be a QObject that declares the interface — StubSettings deliberately
 * does not (see its header). Without one wired via setEngineSettings(),
 * refreshConfigFromSettings() and shouldTileWindow()'s sticky arm both return
 * early and cannot be exercised at all.
 *
 * Every field is public and writable: a test sets the value it cares about and
 * calls the engine's refresh directly, rather than driving a config backend.
 */
class FakeAutotileSettings : public QObject, public PhosphorEngine::IAutotileSettings
{
    Q_OBJECT
    Q_INTERFACES(PhosphorEngine::IAutotileSettings)

public:
    using QObject::QObject;

    QString algorithm = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
    qreal splitRatio = PhosphorTiles::AutotileDefaults::DefaultSplitRatio;
    qreal splitRatioStep = PhosphorTiles::AutotileDefaults::DefaultSplitRatioStep;
    int masterCount = PhosphorTiles::AutotileDefaults::DefaultMasterCount;
    int maxWindows = PhosphorTiles::AutotileDefaults::DefaultMaxWindows;
    PhosphorEngine::StickyWindowHandling stickyHandling = PhosphorEngine::StickyWindowHandling::TreatAsNormal;
    QVariantMap perAlgorithmSettings;

    QString defaultAutotileAlgorithm() const override
    {
        return algorithm;
    }
    qreal autotileSplitRatio() const override
    {
        return splitRatio;
    }
    qreal autotileSplitRatioStep() const override
    {
        return splitRatioStep;
    }
    int autotileMasterCount() const override
    {
        return masterCount;
    }
    int autotileMaxWindows() const override
    {
        return maxWindows;
    }
    PhosphorEngine::StickyWindowHandling autotileStickyWindowHandling() const override
    {
        return stickyHandling;
    }
    QVariantMap autotilePerAlgorithmSettings() const override
    {
        return perAlgorithmSettings;
    }

    // ── Not meaningful to the tests that use this fake — engine defaults ──
    int autotileInnerGap() const override
    {
        return 0;
    }
    int autotileOuterGap() const override
    {
        return 0;
    }
    bool autotileUsePerSideOuterGap() const override
    {
        return false;
    }
    int autotileOuterGapTop() const override
    {
        return 0;
    }
    int autotileOuterGapBottom() const override
    {
        return 0;
    }
    int autotileOuterGapLeft() const override
    {
        return 0;
    }
    int autotileOuterGapRight() const override
    {
        return 0;
    }
    bool autotileFocusNewWindows() const override
    {
        return false;
    }
    bool autotileSmartGaps() const override
    {
        return false;
    }
    bool autotileFocusFollowsMouse() const override
    {
        return false;
    }
    bool autotileRespectMinimumSize() const override
    {
        return false;
    }
    PhosphorTiles::AutotileInsertPosition autotileInsertPosition() const override
    {
        return PhosphorTiles::AutotileInsertPosition::End;
    }
    PhosphorTiles::AutotileOverflowBehavior autotileOverflowBehavior() const override
    {
        return PhosphorTiles::AutotileOverflowBehavior::Float;
    }

    // ── Write-backs: recorded, not persisted ──
    void setDefaultAutotileAlgorithm(const QString& algorithmId) override
    {
        algorithm = algorithmId;
    }
    void setAutotileSplitRatio(qreal ratio) override
    {
        splitRatio = ratio;
    }
    void setAutotileMasterCount(int count) override
    {
        masterCount = count;
    }
    void setAutotileMaxWindows(int max) override
    {
        maxWindows = max;
    }
    void setAutotilePerAlgorithmSettings(const QVariantMap& settings) override
    {
        perAlgorithmSettings = settings;
    }
    void clearPerScreenAutotileSettings(const QString&) override
    {
    }
};

/**
 * @brief Minimal IWindowTrackingService whose only meaningful read is stickiness.
 *
 * AutotileEngine::shouldTileWindow() consults the tracker for
 * isWindowSticky(), which is otherwise unreachable in a headless test (the
 * engine is normally built with a null tracker). Everything else returns the
 * "nothing known about this window" default, which is what the engine's own
 * null-tracker path already assumes.
 *
 * It is a QObject carrying windowZoneChanged because AutotileEngine::
 * connectSignals() connects that signal on any non-null tracker and Q_ASSERTs
 * the connection — asQObject() returning nullptr aborts a debug build.
 */
class FakeStickyWindowTracking : public QObject, public PhosphorEngine::IWindowTrackingService
{
    Q_OBJECT

public:
    using QObject::QObject;

    QSet<QString> stickyWindows;

    bool isWindowSticky(const QString& windowId) const override
    {
        return stickyWindows.contains(windowId);
    }
    PhosphorEngine::WindowPlacementStore& placementStore() override
    {
        return m_store;
    }
    const QHash<QString, QList<PhosphorEngine::PendingRestore>>& pendingRestoreQueues() const override
    {
        return m_pending;
    }
    QObject* asQObject() override
    {
        return this;
    }

Q_SIGNALS:
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

public:
    // ── Unused by these tests — "nothing known" stubs ──
    QString zoneForWindow(const QString&) const override
    {
        return {};
    }
    QString screenForWindow(const QString&) const override
    {
        return {};
    }
    QString screenForWindow(const QString&, const QString& def) const override
    {
        return def;
    }
    QStringList windowsInZone(const QString&) const override
    {
        return {};
    }
    QRect zoneGeometry(const QString&, const QString& = QString()) const override
    {
        return {};
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
    QStringList zonesForWindow(const QString&) const override
    {
        return {};
    }
    bool isWindowSnapped(const QString&) const override
    {
        return false;
    }
    QString findEmptyZone(const QString& = QString()) const override
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
    void updateLastUsedZone(const QString&, const QString&, const QString&, int) override
    {
    }
    QString currentAppIdFor(const QString&) const override
    {
        return {};
    }
    std::optional<QRect> validatedUnmanagedGeometry(const QString&, const QString&, bool = false) const override
    {
        return std::nullopt;
    }
    void recordFreeGeometry(const QString&, const QString&, const QRect&, bool) override
    {
    }
    void clearFreeGeometry(const QString&) override
    {
    }
    QRect resolveZoneGeometry(const QStringList&, const QString&) const override
    {
        return {};
    }
    QString resolveEffectiveScreenId(const QString& s) const override
    {
        return s;
    }
    QString findEmptyZoneInLayout(PhosphorZones::Layout*, const QString&, int = 0) const override
    {
        return {};
    }
    QSet<QUuid> buildOccupiedZoneSet(const QString& = QString(), int = 0) const override
    {
        return {};
    }
    QVector<PhosphorEngine::ResnapEntry> takeResnapBuffer() override
    {
        return {};
    }

private:
    QHash<QString, QList<PhosphorEngine::PendingRestore>> m_pending;
    PhosphorEngine::WindowPlacementStore m_store;
};

} // namespace PlasmaZones::TestHelpers
