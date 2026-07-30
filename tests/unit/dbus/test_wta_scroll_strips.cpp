// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_scroll_strips.cpp
 * @brief The WindowTrackingAdaptor leg of scrolling strip-structure
 *        persistence.
 *
 * The engine's own serialize/deserialize is covered by
 * test_scrollengine_persistence. What is pinned HERE is the adaptor half
 * nothing else touches:
 *
 *  1. saveState() only consults the provider when DirtyScrollStrips is set,
 *     so an unrelated save cannot overwrite the stored blob.
 *  2. A non-empty provider blob reaches disk and comes back verbatim through
 *     the next adaptor's ctor load.
 *  3. An empty blob DELETES the key rather than writing "{}", and a provider
 *     that was cleared (the shutdown teardown passes {}) reads as empty —
 *     which is exactly why the deletion branch must be gated on the dirty
 *     bit: a save with the provider already detached must not wipe strips
 *     that are still valid on disk.
 */

#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using WTS = PhosphorPlacement::WindowTrackingService;

namespace {
QJsonObject sampleStripBlob()
{
    QJsonObject inner;
    inner[QStringLiteral("columns")] = 3;
    inner[QStringLiteral("focus")] = 1;
    QJsonObject blob;
    blob[QStringLiteral("DP-1|0|")] = inner;
    return blob;
}
} // namespace

class TestWtaScrollStrips : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
    }

    void cleanup()
    {
        destroyAdaptor();
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_guard.reset();
    }

    // A blob provided under a dirty DirtyScrollStrips survives a full
    // save → construct-fresh-adaptor → load round trip.
    void testDirtyStripsRoundTrip()
    {
        makeAdaptor();
        const QJsonObject blob = sampleStripBlob();
        m_wta->setScrollStripStateProvider([blob]() {
            return blob;
        });
        m_wta->service()->markDirty(WTS::DirtyScrollStrips);
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        QCOMPARE(m_wta->loadedScrollStripState(), blob);
    }

    // The gate: with DirtyScrollStrips clear, saveState must not call the
    // provider at all, so a stored blob outlives an unrelated save. Without
    // the gate the null/empty provider on this path would delete the key.
    void testCleanBitLeavesStoredBlobAlone()
    {
        makeAdaptor();
        const QJsonObject blob = sampleStripBlob();
        m_wta->setScrollStripStateProvider([blob]() {
            return blob;
        });
        m_wta->service()->markDirty(WTS::DirtyScrollStrips);
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        // Fresh adaptor, provider deliberately NOT wired (the shape a
        // daemon restart has before the engine exists), and a save driven by
        // a different dirty bit entirely.
        makeAdaptor();
        bool providerCalled = false;
        m_wta->setScrollStripStateProvider([&providerCalled]() {
            providerCalled = true;
            return QJsonObject();
        });
        m_wta->service()->markDirty(WTS::DirtyZoneAssignments);
        m_wta->saveStateOnShutdown();
        QVERIFY(!providerCalled);
        destroyAdaptor();

        makeAdaptor();
        QCOMPARE(m_wta->loadedScrollStripState(), blob);
    }

    // An empty blob under a dirty bit clears the key, so a session that
    // ended with no strips does not restore the previous session's.
    void testEmptyBlobDeletesKey()
    {
        makeAdaptor();
        const QJsonObject blob = sampleStripBlob();
        m_wta->setScrollStripStateProvider([blob]() {
            return blob;
        });
        m_wta->service()->markDirty(WTS::DirtyScrollStrips);
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        QCOMPARE(m_wta->loadedScrollStripState(), blob); // positive control
        m_wta->setScrollStripStateProvider([]() {
            return QJsonObject();
        });
        m_wta->service()->markDirty(WTS::DirtyScrollStrips);
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        QVERIFY(m_wta->loadedScrollStripState().isEmpty());
    }

private:
    void makeAdaptor()
    {
        m_parent = new QObject(nullptr);
        m_wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, m_parent);
    }

    void destroyAdaptor()
    {
        if (!m_parent) {
            return;
        }
        // Detach before teardown, matching the contract the provider setter
        // documents (the closures outlive nothing here, but the daemon's
        // shutdown does exactly this and the test should not diverge).
        if (m_wta) {
            m_wta->setScrollStripStateProvider({});
        }
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
};

QTEST_MAIN(TestWtaScrollStrips)
#include "test_wta_scroll_strips.moc"
