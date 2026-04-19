// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_reactive_metadata.cpp
 * @brief Regression tests for the reactive metadataChanged path.
 *
 * WindowTrackingAdaptor subscribes to WindowRegistry::metadataChanged in
 * setWindowRegistry(). Per feedback_class_change_exclusion.md the handler
 * must update internal class tracking but NEVER retroactively unsnap,
 * re-snap, or re-evaluate rules on committed state.
 *
 * Concretely: lastUsedZoneClass is a human-readable tag stamped on the
 * last-used-zone tracking when a user snaps a window. If that window later
 * renames itself (Electron/CEF apps like Emby), the tag must refresh to
 * the new class so the next auto-snap-by-class check matches against the
 * live value — BUT the window's own zone assignment stays put, because
 * re-evaluating rules on a committed window would surprise users.
 */

#include <QCoreApplication>
#include <QRectF>
#include <QSignalSpy>
#include <QTest>
#include <memory>

#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include "core/layoutmanager.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowregistry.h"
#include "core/windowtrackingservice.h"
#include <PhosphorZones/Zone.h>
#include "dbus/windowtrackingadaptor.h"

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// ─────────────────────────────────────────────────────────────────────────
// Minimal zone-detector stub — the reactive tests don't exercise detection
// ─────────────────────────────────────────────────────────────────────────

class StubZoneDetectorReactive : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorReactive(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(PhosphorZones::Layout* layout) override
    {
        m_layout = layout;
    }
    PhosphorZones::ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    PhosphorZones::Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<PhosphorZones::Zone*> expandPaintedZonesToRect(const QVector<PhosphorZones::Zone*>&) const override
    {
        return {};
    }
    void highlightZone(PhosphorZones::Zone*) override
    {
    }
    void highlightZones(const QVector<PhosphorZones::Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    PhosphorZones::Layout* m_layout = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────
// Fixture helpers
// ─────────────────────────────────────────────────────────────────────────

static PhosphorZones::Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new PhosphorZones::Zone(layout);
        const qreal x = static_cast<qreal>(i) / zoneCount;
        const qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

class TestWtaReactiveMetadata : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = makePzLayoutManager(nullptr).release();
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetectorReactive(nullptr);
        m_registry = new WindowRegistry(nullptr);

        m_parent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, m_parent);
        m_wta->setWindowRegistry(m_registry);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
        m_screenId = QStringLiteral("DP-1");
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;

        delete m_registry;
        m_registry = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;

        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // ────────────────────────────────────────────────────────────────────
    // The substantive test — Emby scenario end-to-end through the adaptor
    // ────────────────────────────────────────────────────────────────────

    void lastUsedZoneClass_retagsOnClassMutation()
    {
        WindowTrackingService* service = m_wta->service();
        QVERIFY(service);

        const QString instanceId = QStringLiteral("cef1ba31-3316-4f05-84f5-ef627674b504");
        const QString classA = QStringLiteral("emby-beta");
        const QString classB = QStringLiteral("media.emby.client.beta");

        // 1. Effect registers the window's initial metadata before snapping.
        m_registry->upsert(instanceId, {classA, QString(), QString()});

        // 2. User snap: WTA's snapped handler stamps the live class onto
        //    m_lastUsedZoneClass via updateLastUsedZone. We call
        //    updateLastUsedZone directly since windowSnapped has extra moving
        //    parts (screen resolution, etc.) that aren't relevant here.
        service->updateLastUsedZone(m_zoneIds[0], m_screenId, classA, 1);
        QCOMPARE(service->lastUsedZoneClass(), classA);
        QCOMPARE(service->lastUsedZoneId(), m_zoneIds[0]);

        // Also give the service an assignment so we can verify committed
        // state is preserved across the metadata update.
        service->assignWindowToZone(instanceId, m_zoneIds[0], m_screenId, 1);
        QVERIFY(service->isWindowSnapped(instanceId));

        // 3. KWin rebroadcasts the window with a new class. Registry fires
        //    metadataChanged, WTA's lambda must (a) retag the class tracking
        //    and (b) leave the assignment alone.
        m_registry->upsert(instanceId, {classB, QString(), QString()});
        QCoreApplication::processEvents();

        // Class tag is updated.
        QCOMPARE(service->lastUsedZoneClass(), classB);
        // PhosphorZones::Zone id unchanged — NOT a retroactive move.
        QCOMPARE(service->lastUsedZoneId(), m_zoneIds[0]);
        // Committed snap state is preserved per feedback_class_change_exclusion.md.
        QVERIFY(service->isWindowSnapped(instanceId));
        QCOMPARE(service->zoneForWindow(instanceId), m_zoneIds[0]);
    }

    // ────────────────────────────────────────────────────────────────────
    // Guard: when another live window still owns the old class, the tag
    // must NOT change. Otherwise a second-instance rename would silently
    // corrupt the last-used-zone tracking for the first instance.
    // ────────────────────────────────────────────────────────────────────

    void lastUsedZoneClass_preservedWhenOtherInstancesOwnOldClass()
    {
        WindowTrackingService* service = m_wta->service();

        const QString instanceA = QStringLiteral("uuid-A");
        const QString instanceB = QStringLiteral("uuid-B");
        const QString classA = QStringLiteral("firefox");
        const QString classB = QStringLiteral("firefox-nightly");

        // Two live instances share the same class; last-used-zone is
        // stamped with that class.
        m_registry->upsert(instanceA, {classA, QString(), QString()});
        m_registry->upsert(instanceB, {classA, QString(), QString()});
        service->updateLastUsedZone(m_zoneIds[1], m_screenId, classA, 1);
        QCOMPARE(service->lastUsedZoneClass(), classA);

        // Instance B renames. Instance A still owns classA, so the tag
        // must remain classA — it's still meaningful for the surviving
        // instance(s).
        m_registry->upsert(instanceB, {classB, QString(), QString()});
        QCoreApplication::processEvents();

        QCOMPARE(service->lastUsedZoneClass(), classA);
    }

    // ────────────────────────────────────────────────────────────────────
    // Guard: if the renamed instance didn't set the tag, nothing should
    // happen. The retag is targeted — generic renames are no-ops.
    // ────────────────────────────────────────────────────────────────────

    void lastUsedZoneClass_unaffectedByUnrelatedRename()
    {
        WindowTrackingService* service = m_wta->service();

        m_registry->upsert(QStringLiteral("uuid-firefox"), {QStringLiteral("firefox"), QString(), QString()});
        m_registry->upsert(QStringLiteral("uuid-kate"), {QStringLiteral("kate"), QString(), QString()});

        service->updateLastUsedZone(m_zoneIds[2], m_screenId, QStringLiteral("firefox"), 1);

        // kate renames to kate-beta — firefox tag is untouched.
        m_registry->upsert(QStringLiteral("uuid-kate"), {QStringLiteral("kate-beta"), QString(), QString()});
        QCoreApplication::processEvents();

        QCOMPARE(service->lastUsedZoneClass(), QStringLiteral("firefox"));
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetectorReactive* m_zoneDetector = nullptr;
    WindowRegistry* m_registry = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtaReactiveMetadata)
#include "test_wta_reactive_metadata.moc"
