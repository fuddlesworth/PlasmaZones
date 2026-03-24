// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_convenience.cpp
 * @brief Unit tests for WindowTrackingAdaptor convenience methods:
 *        moveWindowToZone, swapWindowsById, getWindowState, getAllWindowStates,
 *        and windowStateChanged signal emission.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRectF>
#include <memory>

#include "core/windowtrackingservice.h"
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "dbus/windowtrackingadaptor.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

class StubSettingsConvenience : public ISettings
{
    Q_OBJECT
public:
    explicit StubSettingsConvenience(QObject* parent = nullptr)
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
    bool filterLayoutsByAspectRatio() const override
    {
        return true;
    }
    void setFilterLayoutsByAspectRatio(bool) override
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
    QColor autotileInactiveBorderColor() const override
    {
        return {};
    }
    void setAutotileInactiveBorderColor(const QColor&) override
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

// =========================================================================
// Stub Zone Detector
// =========================================================================

class StubZoneDetectorConvenience : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorConvenience(QObject* parent = nullptr)
        : IZoneDetector(parent)
    {
    }
    Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(Layout* layout) override
    {
        m_layout = layout;
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

private:
    Layout* m_layout = nullptr;
};

// =========================================================================
// Helpers
// =========================================================================

static Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new Zone(layout);
        qreal x = static_cast<qreal>(i) / zoneCount;
        qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

// =========================================================================
// Test Class
// =========================================================================

class TestWtaConvenience : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsConvenience(nullptr);
        m_zoneDetector = new StubZoneDetectorConvenience(nullptr);

        // WTA needs a parent QObject for QDBusAbstractAdaptor
        m_parent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, m_settings, nullptr, m_parent);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }

        m_screenId = QStringLiteral("DP-1");
    }

    void cleanup()
    {
        // WTA is owned by m_parent (QDBusAbstractAdaptor parent)
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;
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

    // =====================================================================
    // moveWindowToZone
    // =====================================================================

    void testMoveWindowToZone_validZone_emitsApplyGeometry()
    {
        QString windowId = QStringLiteral("firefox|12345");

        // Assign a screen mapping so resolveScreenForSnap works
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_wta->moveWindowToZone(windowId, m_zoneIds[0]);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), windowId);
        // geometryJson should be non-empty
        QVERIFY(!spy.at(0).at(1).toString().isEmpty());
        QCOMPARE(spy.at(0).at(2).toString(), m_zoneIds[0]);
    }

    void testMoveWindowToZone_invalidZone_noSignal()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_wta->moveWindowToZone(windowId, QStringLiteral("nonexistent-zone-id"));

        QCOMPARE(spy.count(), 0);
    }

    void testMoveWindowToZone_emptyWindowId_noSignal()
    {
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_wta->moveWindowToZone(QString(), m_zoneIds[0]);

        QCOMPARE(spy.count(), 0);
    }

    // =====================================================================
    // swapWindowsById
    // =====================================================================

    void testSwapWindowsById_twoSnappedWindows_emitsTwoApplyGeometry()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        // Snap both windows to different zones via the WTA's windowSnapped slot
        m_wta->windowSnapped(window1, m_zoneIds[0], m_screenId);
        m_wta->windowSnapped(window2, m_zoneIds[1], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_wta->swapWindowsById(window1, window2);

        QCOMPARE(spy.count(), 2);

        // Window1 should move to zone2, window2 to zone1
        QCOMPARE(spy.at(0).at(0).toString(), window1);
        QCOMPARE(spy.at(0).at(2).toString(), m_zoneIds[1]);
        QCOMPARE(spy.at(1).at(0).toString(), window2);
        QCOMPARE(spy.at(1).at(2).toString(), m_zoneIds[0]);
    }

    void testSwapWindowsById_oneNotSnapped_noSignal()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        // Only snap window1
        m_wta->windowSnapped(window1, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_wta->swapWindowsById(window1, window2);

        QCOMPARE(spy.count(), 0);
    }

    // =====================================================================
    // getWindowState
    // =====================================================================

    void testGetWindowState_snappedWindow_returnsJson()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_wta->windowSnapped(windowId, m_zoneIds[0], m_screenId);

        QString stateJson = m_wta->getWindowState(windowId);
        QJsonDocument doc = QJsonDocument::fromJson(stateJson.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QCOMPARE(obj[QLatin1String("windowId")].toString(), windowId);
        QCOMPARE(obj[QLatin1String("zoneId")].toString(), m_zoneIds[0]);
        QCOMPARE(obj[QLatin1String("screenId")].toString(), m_screenId);
        QCOMPARE(obj[QLatin1String("isFloating")].toBool(), false);
    }

    void testGetWindowState_floatingWindow_returnsFloatingTrue()
    {
        QString windowId = QStringLiteral("firefox|12345");

        // Snap then float
        m_wta->windowSnapped(windowId, m_zoneIds[0], m_screenId);
        m_wta->setWindowFloating(windowId, true);

        QString stateJson = m_wta->getWindowState(windowId);
        QJsonDocument doc = QJsonDocument::fromJson(stateJson.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QCOMPARE(obj[QLatin1String("isFloating")].toBool(), true);
    }

    void testGetWindowState_unknownWindow_returnsEmptyZone()
    {
        QString windowId = QStringLiteral("unknown|99999");

        QString stateJson = m_wta->getWindowState(windowId);
        QJsonDocument doc = QJsonDocument::fromJson(stateJson.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QVERIFY(obj[QLatin1String("zoneId")].toString().isEmpty());
    }

    // =====================================================================
    // getAllWindowStates
    // =====================================================================

    void testGetAllWindowStates_multipleWindows_returnsArray()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_wta->windowSnapped(window1, m_zoneIds[0], m_screenId);
        m_wta->windowSnapped(window2, m_zoneIds[1], m_screenId);

        QString allStatesJson = m_wta->getAllWindowStates();
        QJsonDocument doc = QJsonDocument::fromJson(allStatesJson.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isArray());

        QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), 2);

        // Collect all window IDs from the array
        QStringList windowIds;
        for (const QJsonValue& val : arr) {
            windowIds.append(val.toObject()[QLatin1String("windowId")].toString());
        }
        QVERIFY(windowIds.contains(window1));
        QVERIFY(windowIds.contains(window2));
    }

    // =====================================================================
    // windowStateChanged signal
    // =====================================================================

    void testWindowStateChanged_emittedOnSnap()
    {
        QString windowId = QStringLiteral("firefox|12345");

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_wta->windowSnapped(windowId, m_zoneIds[0], m_screenId);

        QVERIFY(spy.count() >= 1);

        // Find the "snapped" emission
        bool foundSnapped = false;
        for (int i = 0; i < spy.count(); ++i) {
            QJsonDocument doc = QJsonDocument::fromJson(spy.at(i).at(1).toString().toUtf8());
            QJsonObject obj = doc.object();
            if (obj[QLatin1String("changeType")].toString() == QLatin1String("snapped")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundSnapped = true;
                break;
            }
        }
        QVERIFY(foundSnapped);
    }

    void testWindowStateChanged_emittedOnUnsnap()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_wta->windowSnapped(windowId, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_wta->windowUnsnapped(windowId);

        QVERIFY(spy.count() >= 1);

        bool foundUnsnapped = false;
        for (int i = 0; i < spy.count(); ++i) {
            QJsonDocument doc = QJsonDocument::fromJson(spy.at(i).at(1).toString().toUtf8());
            QJsonObject obj = doc.object();
            if (obj[QLatin1String("changeType")].toString() == QLatin1String("unsnapped")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundUnsnapped = true;
                break;
            }
        }
        QVERIFY(foundUnsnapped);
    }

    void testWindowStateChanged_emittedOnFloat()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_wta->windowSnapped(windowId, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_wta->setWindowFloating(windowId, true);

        QVERIFY(spy.count() >= 1);

        bool foundFloated = false;
        for (int i = 0; i < spy.count(); ++i) {
            QJsonDocument doc = QJsonDocument::fromJson(spy.at(i).at(1).toString().toUtf8());
            QJsonObject obj = doc.object();
            if (obj[QLatin1String("changeType")].toString() == QLatin1String("floated")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundFloated = true;
                break;
            }
        }
        QVERIFY(foundFloated);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsConvenience* m_settings = nullptr;
    StubZoneDetectorConvenience* m_zoneDetector = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtaConvenience)
#include "test_wta_convenience.moc"
