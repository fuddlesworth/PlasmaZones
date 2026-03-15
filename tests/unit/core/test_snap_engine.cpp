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

// ISettings has ~100 pure virtuals; include the full stub from test_wts_lifecycle via macro
// to avoid 600 lines of boilerplate. Each test file uses uniquely named stubs for ODR safety.
#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "../helpers/IsolatedConfigGuard.h"

// Reuse the proven stub pattern from test_wts_lifecycle (full ISettings + IZoneDetector impl)
// Defined inline with unique names to avoid ODR conflicts across test TUs.

class StubSettingsSnap : public ISettings
{
    Q_OBJECT
public:
    explicit StubSettingsSnap(QObject* parent = nullptr)
        : ISettings(parent)
    {
    }
    bool shiftDragToActivate() const override
    {
        return false;
    }
    void setShiftDragToActivate(bool) override
    {
    }
    QVariantList dragActivationTriggers() const override
    {
        return {};
    }
    void setDragActivationTriggers(const QVariantList&) override
    {
    }
    bool zoneSpanEnabled() const override
    {
        return false;
    }
    void setZoneSpanEnabled(bool) override
    {
    }
    DragModifier zoneSpanModifier() const override
    {
        return DragModifier::Disabled;
    }
    void setZoneSpanModifier(DragModifier) override
    {
    }
    QVariantList zoneSpanTriggers() const override
    {
        return {};
    }
    void setZoneSpanTriggers(const QVariantList&) override
    {
    }
    bool toggleActivation() const override
    {
        return false;
    }
    void setToggleActivation(bool) override
    {
    }
    bool snappingEnabled() const override
    {
        return true;
    }
    void setSnappingEnabled(bool) override
    {
    }
    bool showZonesOnAllMonitors() const override
    {
        return false;
    }
    void setShowZonesOnAllMonitors(bool) override
    {
    }
    QStringList disabledMonitors() const override
    {
        return {};
    }
    void setDisabledMonitors(const QStringList&) override
    {
    }
    bool isMonitorDisabled(const QString&) const override
    {
        return false;
    }
    bool showZoneNumbers() const override
    {
        return true;
    }
    void setShowZoneNumbers(bool) override
    {
    }
    bool flashZonesOnSwitch() const override
    {
        return false;
    }
    void setFlashZonesOnSwitch(bool) override
    {
    }
    bool showOsdOnLayoutSwitch() const override
    {
        return false;
    }
    void setShowOsdOnLayoutSwitch(bool) override
    {
    }
    bool showNavigationOsd() const override
    {
        return false;
    }
    void setShowNavigationOsd(bool) override
    {
    }
    OsdStyle osdStyle() const override
    {
        return OsdStyle::None;
    }
    void setOsdStyle(OsdStyle) override
    {
    }
    OverlayDisplayMode overlayDisplayMode() const override
    {
        return OverlayDisplayMode::ZoneRectangles;
    }
    void setOverlayDisplayMode(OverlayDisplayMode) override
    {
    }
    bool useSystemColors() const override
    {
        return false;
    }
    void setUseSystemColors(bool) override
    {
    }
    QColor highlightColor() const override
    {
        return Qt::blue;
    }
    void setHighlightColor(const QColor&) override
    {
    }
    QColor inactiveColor() const override
    {
        return Qt::gray;
    }
    void setInactiveColor(const QColor&) override
    {
    }
    QColor borderColor() const override
    {
        return Qt::white;
    }
    void setBorderColor(const QColor&) override
    {
    }
    QColor labelFontColor() const override
    {
        return Qt::white;
    }
    void setLabelFontColor(const QColor&) override
    {
    }
    qreal activeOpacity() const override
    {
        return 0.5;
    }
    void setActiveOpacity(qreal) override
    {
    }
    qreal inactiveOpacity() const override
    {
        return 0.3;
    }
    void setInactiveOpacity(qreal) override
    {
    }
    int borderWidth() const override
    {
        return 2;
    }
    void setBorderWidth(int) override
    {
    }
    int borderRadius() const override
    {
        return 8;
    }
    void setBorderRadius(int) override
    {
    }
    bool enableBlur() const override
    {
        return false;
    }
    void setEnableBlur(bool) override
    {
    }
    QString labelFontFamily() const override
    {
        return {};
    }
    void setLabelFontFamily(const QString&) override
    {
    }
    qreal labelFontSizeScale() const override
    {
        return 1.0;
    }
    void setLabelFontSizeScale(qreal) override
    {
    }
    int labelFontWeight() const override
    {
        return 400;
    }
    void setLabelFontWeight(int) override
    {
    }
    bool labelFontItalic() const override
    {
        return false;
    }
    void setLabelFontItalic(bool) override
    {
    }
    bool labelFontUnderline() const override
    {
        return false;
    }
    void setLabelFontUnderline(bool) override
    {
    }
    bool labelFontStrikeout() const override
    {
        return false;
    }
    void setLabelFontStrikeout(bool) override
    {
    }
    bool enableShaderEffects() const override
    {
        return false;
    }
    void setEnableShaderEffects(bool) override
    {
    }
    int shaderFrameRate() const override
    {
        return 60;
    }
    void setShaderFrameRate(int) override
    {
    }
    bool enableAudioVisualizer() const override
    {
        return false;
    }
    void setEnableAudioVisualizer(bool) override
    {
    }
    int audioSpectrumBarCount() const override
    {
        return 32;
    }
    void setAudioSpectrumBarCount(int) override
    {
    }
    int zonePadding() const override
    {
        return 8;
    }
    void setZonePadding(int) override
    {
    }
    int outerGap() const override
    {
        return 8;
    }
    void setOuterGap(int) override
    {
    }
    bool usePerSideOuterGap() const override
    {
        return false;
    }
    void setUsePerSideOuterGap(bool) override
    {
    }
    int outerGapTop() const override
    {
        return 8;
    }
    void setOuterGapTop(int) override
    {
    }
    int outerGapBottom() const override
    {
        return 8;
    }
    void setOuterGapBottom(int) override
    {
    }
    int outerGapLeft() const override
    {
        return 8;
    }
    void setOuterGapLeft(int) override
    {
    }
    int outerGapRight() const override
    {
        return 8;
    }
    void setOuterGapRight(int) override
    {
    }
    int adjacentThreshold() const override
    {
        return 20;
    }
    void setAdjacentThreshold(int) override
    {
    }
    int pollIntervalMs() const override
    {
        return 50;
    }
    void setPollIntervalMs(int) override
    {
    }
    int minimumZoneSizePx() const override
    {
        return 100;
    }
    void setMinimumZoneSizePx(int) override
    {
    }
    int minimumZoneDisplaySizePx() const override
    {
        return 10;
    }
    void setMinimumZoneDisplaySizePx(int) override
    {
    }
    QStringList excludedApplications() const override
    {
        return {};
    }
    void setExcludedApplications(const QStringList&) override
    {
    }
    QStringList excludedWindowClasses() const override
    {
        return {};
    }
    void setExcludedWindowClasses(const QStringList&) override
    {
    }
    bool excludeTransientWindows() const override
    {
        return false;
    }
    void setExcludeTransientWindows(bool) override
    {
    }
    int minimumWindowWidth() const override
    {
        return 0;
    }
    void setMinimumWindowWidth(int) override
    {
    }
    int minimumWindowHeight() const override
    {
        return 0;
    }
    void setMinimumWindowHeight(int) override
    {
    }
    bool isWindowExcluded(const QString&, const QString&) const override
    {
        return false;
    }
    bool zoneSelectorEnabled() const override
    {
        return true;
    }
    void setZoneSelectorEnabled(bool) override
    {
    }
    int zoneSelectorTriggerDistance() const override
    {
        return 50;
    }
    void setZoneSelectorTriggerDistance(int) override
    {
    }
    ZoneSelectorPosition zoneSelectorPosition() const override
    {
        return ZoneSelectorPosition::Top;
    }
    void setZoneSelectorPosition(ZoneSelectorPosition) override
    {
    }
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override
    {
        return ZoneSelectorLayoutMode::Grid;
    }
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode) override
    {
    }
    int zoneSelectorPreviewWidth() const override
    {
        return 180;
    }
    void setZoneSelectorPreviewWidth(int) override
    {
    }
    int zoneSelectorPreviewHeight() const override
    {
        return 101;
    }
    void setZoneSelectorPreviewHeight(int) override
    {
    }
    bool zoneSelectorPreviewLockAspect() const override
    {
        return true;
    }
    void setZoneSelectorPreviewLockAspect(bool) override
    {
    }
    int zoneSelectorGridColumns() const override
    {
        return 5;
    }
    void setZoneSelectorGridColumns(int) override
    {
    }
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override
    {
        return ZoneSelectorSizeMode::Auto;
    }
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode) override
    {
    }
    int zoneSelectorMaxRows() const override
    {
        return 4;
    }
    void setZoneSelectorMaxRows(int) override
    {
    }
    bool keepWindowsInZonesOnResolutionChange() const override
    {
        return true;
    }
    void setKeepWindowsInZonesOnResolutionChange(bool) override
    {
    }
    bool moveNewWindowsToLastZone() const override
    {
        return false;
    }
    void setMoveNewWindowsToLastZone(bool) override
    {
    }
    bool restoreOriginalSizeOnUnsnap() const override
    {
        return true;
    }
    void setRestoreOriginalSizeOnUnsnap(bool) override
    {
    }
    StickyWindowHandling stickyWindowHandling() const override
    {
        return StickyWindowHandling::TreatAsNormal;
    }
    void setStickyWindowHandling(StickyWindowHandling) override
    {
    }
    bool restoreWindowsToZonesOnLogin() const override
    {
        return true;
    }
    void setRestoreWindowsToZonesOnLogin(bool) override
    {
    }
    bool snapAssistFeatureEnabled() const override
    {
        return false;
    }
    void setSnapAssistFeatureEnabled(bool) override
    {
    }
    bool snapAssistEnabled() const override
    {
        return false;
    }
    void setSnapAssistEnabled(bool) override
    {
    }
    QVariantList snapAssistTriggers() const override
    {
        return {};
    }
    void setSnapAssistTriggers(const QVariantList&) override
    {
    }
    QString defaultLayoutId() const override
    {
        return {};
    }
    void setDefaultLayoutId(const QString&) override
    {
    }
    bool animationsEnabled() const override
    {
        return false;
    }
    void setAnimationsEnabled(bool) override
    {
    }
    int animationDuration() const override
    {
        return 200;
    }
    void setAnimationDuration(int) override
    {
    }
    QString animationEasingCurve() const override
    {
        return {};
    }
    void setAnimationEasingCurve(const QString&) override
    {
    }
    int animationMinDistance() const override
    {
        return 10;
    }
    void setAnimationMinDistance(int) override
    {
    }
    int animationSequenceMode() const override
    {
        return 0;
    }
    void setAnimationSequenceMode(int) override
    {
    }
    int animationStaggerInterval() const override
    {
        return 50;
    }
    void setAnimationStaggerInterval(int) override
    {
    }
    bool autotileFocusFollowsMouse() const override
    {
        return false;
    }
    void setAutotileFocusFollowsMouse(bool) override
    {
    }
    bool autotileHideTitleBars() const override
    {
        return false;
    }
    void setAutotileHideTitleBars(bool) override
    {
    }
    bool autotileShowBorder() const override
    {
        return false;
    }
    void setAutotileShowBorder(bool) override
    {
    }
    int autotileBorderWidth() const override
    {
        return 2;
    }
    void setAutotileBorderWidth(int) override
    {
    }
    int autotileBorderRadius() const override
    {
        return 0;
    }
    void setAutotileBorderRadius(int) override
    {
    }
    QColor autotileBorderColor() const override
    {
        return Qt::white;
    }
    void setAutotileBorderColor(const QColor&) override
    {
    }
    bool autotileUseSystemBorderColors() const override
    {
        return false;
    }
    void setAutotileUseSystemBorderColors(bool) override
    {
    }
    QStringList lockedScreens() const override
    {
        return {};
    }
    void setLockedScreens(const QStringList&) override
    {
    }
    bool isScreenLocked(const QString&) const override
    {
        return false;
    }
    void setScreenLocked(const QString&, bool) override
    {
    }
    bool isContextLocked(const QString&, int, const QString&) const override
    {
        return false;
    }
    void setContextLocked(const QString&, int, const QString&, bool) override
    {
    }
    void load() override
    {
    }
    void save() override
    {
    }
    void reset() override
    {
    }
};

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

        engine.windowFocused(QStringLiteral("app|uuid1"), QStringLiteral("DP-2"));

        // Verify indirectly: moveInDirection("") triggers feedback with m_lastActiveScreenName
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);
        engine.moveInDirection(QString()); // empty direction triggers feedback
        QCOMPARE(feedbackSpy.count(), 1);
        QCOMPARE(feedbackSpy.at(0).at(5).toString(), QStringLiteral("DP-2"));
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
    // clearFloatingStateForSnap tests
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
