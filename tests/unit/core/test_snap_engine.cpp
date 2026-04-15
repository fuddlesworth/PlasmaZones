// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include "snap/SnapEngine.h"
#include "core/windowtrackingservice.h"
#include "core/layoutmanager.h"
#include "core/interfaces.h"

using namespace PlasmaZones;

// =========================================================================
// Minimal stubs for WTS constructor assertions
// =========================================================================

#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"

using StubSettingsSnap = StubSettings;

class StubZoneDetectorSnap : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSnap(QObject* parent = nullptr)
        : IZoneDetector(parent)
    {
    }
    Layout* layout() const override
    {
        return nullptr;
    }
    void setLayout(Layout*) override
    {
    }
    ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>&) const override
    {
        return {};
    }
    void highlightZone(Zone*) override
    {
    }
    void highlightZones(const QVector<Zone*>&) override
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
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsSnap* m_settings = nullptr;
    StubZoneDetectorSnap* m_zoneDetector = nullptr;
    WindowTrackingService* m_wts = nullptr;

private Q_SLOTS:

    void init()
    {
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsSnap(nullptr);
        m_zoneDetector = new StubZoneDetectorSnap(nullptr);
        m_wts = new WindowTrackingService(m_layoutManager, m_zoneDetector, m_settings, nullptr, nullptr);
    }

    void cleanup()
    {
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
        const QString windowId = QStringLiteral("app|uuid-snap");
        const QString screenName = QStringLiteral("DP-1");

        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-1"), screenName, 0);
        QVERIFY(m_wts->isWindowSnapped(windowId));
        QVERIFY(!m_wts->isWindowFloating(windowId));

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
        engine.saveState();
        engine.loadState();
    }

    void testSaveLoadState_bothDelegatesCalled()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
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
