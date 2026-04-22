// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include "snap/SnapEngine.h"
#include "core/windowtrackingservice.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces.h"

using namespace PlasmaZones;

// =========================================================================
// Minimal stubs for WTS constructor assertions
// =========================================================================

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/virtualdesktopmanager.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"

using StubSettingsSnap = StubSettings;

class StubZoneDetectorSnap : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSnap(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return nullptr;
    }
    void setLayout(PhosphorZones::Layout*) override
    {
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
};

/**
 * @brief Unit tests for SnapEngine: screen routing, lifecycle, float state,
 *        signal emission, and persistence delegation.
 */
class TestSnapEngine : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsSnap* m_settings = nullptr;
    StubZoneDetectorSnap* m_zoneDetector = nullptr;
    WindowTrackingService* m_wts = nullptr;
    PhosphorZones::SnapState* m_snapState = nullptr;

private Q_SLOTS:

    void init()
    {
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsSnap(nullptr);
        m_zoneDetector = new StubZoneDetectorSnap(nullptr);
        m_wts = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);
        m_snapState = new PhosphorZones::SnapState(QString(), nullptr);
    }

    void cleanup()
    {
        delete m_snapState;
        delete m_wts;
        delete m_zoneDetector;
        delete m_settings;
        delete m_layoutManager;
    }

    // =========================================================================
    // isActiveOnScreen tests
    // =========================================================================

    void testIsActiveOnScreen_noAutotileEngine_returnsTrue()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);

        // No autotile engine set — SnapEngine owns all screens
        QVERIFY(engine.isActiveOnScreen(QStringLiteral("DP-1")));
        QVERIFY(engine.isActiveOnScreen(QStringLiteral("HDMI-1")));
        QVERIFY(engine.isActiveOnScreen(QString()));
    }

    // =========================================================================
    // windowFocused tests
    // =========================================================================

    void testWindowFocused_updatesLastActiveScreen()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);

        QCOMPARE(engine.lastActiveScreenId(), QString());

        engine.windowFocused(QStringLiteral("app|uuid1"), QStringLiteral("DP-2"));

        QCOMPARE(engine.lastActiveScreenId(), QStringLiteral("DP-2"));
    }

    // =========================================================================
    // windowClosed tests
    // =========================================================================

    void testWindowClosed_doesNotCrash()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);

        engine.windowClosed(QStringLiteral("app|uuid1"));
        engine.windowClosed(QString());
    }

    // =========================================================================
    // WindowTrackingService::clearFloatingForSnap tests
    //
    // (The former SnapEngine::clearFloatingStateForSnap wrapper was removed —
    // all snap-commit paths now go through WindowTrackingService::commitSnap
    // which handles floating-state clearing internally via clearFloatingForSnap.)
    // =========================================================================

    void testClearFloatingForSnap_returnsTrue_whenFloating()
    {
        const QString windowId = QStringLiteral("app|uuid-float");
        m_wts->setWindowFloating(windowId, true);
        QVERIFY(m_wts->isWindowFloating(windowId));

        bool result = m_wts->clearFloatingForSnap(windowId);
        QVERIFY(result);
        QVERIFY(!m_wts->isWindowFloating(windowId));
    }

    void testClearFloatingForSnap_returnsFalse_whenNotFloating()
    {
        const QString windowId = QStringLiteral("app|uuid-nofloat");
        QVERIFY(!m_wts->isWindowFloating(windowId));

        bool result = m_wts->clearFloatingForSnap(windowId);
        QVERIFY(!result);
    }

    // =========================================================================
    // toggleWindowFloat signal tests
    // =========================================================================

    void testToggleWindowFloat_snappedWindow_emitsFloatingTrue()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        const QString windowId = QStringLiteral("app|uuid-snap");
        const QString screenName = QStringLiteral("DP-1");

        m_snapState->assignWindowToZone(windowId, QStringLiteral("zone-1"), screenName, 0);
        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-1"), screenName, 0);
        QVERIFY(m_snapState->isWindowSnapped(windowId));
        QVERIFY(!m_snapState->isFloating(windowId));

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);

        engine.toggleWindowFloat(windowId, screenName);

        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.at(0).at(0).toString(), windowId);
        QCOMPARE(floatSpy.at(0).at(1).toBool(), true);
        QCOMPARE(floatSpy.at(0).at(2).toString(), screenName);

        QCOMPARE(feedbackSpy.count(), 1);
        QCOMPARE(feedbackSpy.at(0).at(0).toBool(), true);
        QCOMPARE(feedbackSpy.at(0).at(2).toString(), QStringLiteral("floated"));
    }

    void testToggleWindowFloat_notSnappedNotFloating_noSignal()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        const QString windowId = QStringLiteral("app|uuid-untracked");
        const QString screenName = QStringLiteral("DP-1");

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);

        engine.toggleWindowFloat(windowId, screenName);

        QCOMPARE(floatSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 0);
    }

    void testSetWindowFloat_true_emitsFloatingChanged()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        const QString windowId = QStringLiteral("app|uuid-setfloat");

        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-1"), QStringLiteral("DP-1"), 0);

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        engine.setWindowFloat(windowId, true);

        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.at(0).at(0).toString(), windowId);
        QCOMPARE(floatSpy.at(0).at(1).toBool(), true);
    }

    void testSetWindowFloat_false_noPreFloatZone_keepsFloating()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        const QString windowId = QStringLiteral("app|uuid-unfloat-fail");

        m_wts->setWindowFloating(windowId, true);

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        engine.setWindowFloat(windowId, false);

        // No pre-float zone → unfloat fails → window stays floating, no signal
        QCOMPARE(floatSpy.count(), 0);
        QVERIFY(m_wts->isWindowFloating(windowId));
    }

    // =========================================================================
    // saveState / loadState persistence delegation tests
    // =========================================================================

    void testSaveState_callsDelegateWhenSet()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        bool saveCalled = false;
        bool loadCalled = false;
        engine.setPersistenceDelegate(
            [&saveCalled]() {
                saveCalled = true;
            },
            [&loadCalled]() {
                loadCalled = true;
            });

        engine.saveState();
        QVERIFY(saveCalled);
        QVERIFY(!loadCalled);
    }

    void testLoadState_callsDelegateWhenSet()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        bool saveCalled = false;
        bool loadCalled = false;
        engine.setPersistenceDelegate(
            [&saveCalled]() {
                saveCalled = true;
            },
            [&loadCalled]() {
                loadCalled = true;
            });

        engine.loadState();
        QVERIFY(!saveCalled);
        QVERIFY(loadCalled);
    }

    void testSaveState_noopWithoutDelegate()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        engine.saveState();
        engine.loadState();
    }

    void testSaveLoadState_bothDelegatesCalled()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setSnapState(m_snapState);
        int saveCount = 0;
        int loadCount = 0;
        engine.setPersistenceDelegate(
            [&saveCount]() {
                saveCount++;
            },
            [&loadCount]() {
                loadCount++;
            });

        engine.saveState();
        engine.saveState();
        engine.loadState();

        QCOMPARE(saveCount, 2);
        QCOMPARE(loadCount, 1);
    }
};

QTEST_GUILESS_MAIN(TestSnapEngine)
#include "test_snap_engine.moc"
