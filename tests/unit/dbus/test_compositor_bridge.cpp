// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_compositor_bridge.cpp
 * @brief Unit tests for CompositorBridgeAdaptor and ControlAdaptor.
 *
 * CompositorBridgeAdaptor: bridge registration, capabilities, modifier state.
 * ControlAdaptor: API version, capabilities, full state snapshot.
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

#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/windowtrackingadaptor.h"
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings (ODR-safe unique name)
// =========================================================================

class StubSettingsBridge : public ISettings
{
    Q_OBJECT
public:
    explicit StubSettingsBridge(QObject* parent = nullptr)
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

class StubZoneDetectorBridge : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorBridge(QObject* parent = nullptr)
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

// =========================================================================
// Test Class
// =========================================================================

class TestCompositorBridge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_bridgeAdaptor = new CompositorBridgeAdaptor(m_parent);

        // For ControlAdaptor tests we need a WTA + LayoutManager
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsBridge(nullptr);
        m_zoneDetector = new StubZoneDetectorBridge(nullptr);

        m_wtaParent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, m_settings, nullptr, m_wtaParent);

        // Create a test layout so getFullState has data
        auto* layout = new Layout(QStringLiteral("TestLayout"), m_layoutManager);
        auto* zone = new Zone(layout);
        zone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        zone->setZoneNumber(1);
        layout->addZone(zone);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        m_controlParent = new QObject(nullptr);
        m_controlAdaptor = new ControlAdaptor(m_wta, nullptr, m_layoutManager, nullptr, m_controlParent);
    }

    void cleanup()
    {
        delete m_controlParent;
        m_controlParent = nullptr;
        m_controlAdaptor = nullptr;
        delete m_wtaParent;
        m_wtaParent = nullptr;
        m_wta = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        delete m_parent;
        m_parent = nullptr;
        m_bridgeAdaptor = nullptr;
        m_guard.reset();
    }

    // =====================================================================
    // CompositorBridgeAdaptor: registerBridge
    // =====================================================================

    void testRegisterBridge_returnsApiVersion()
    {
        QString result = m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("6.0"),
                                                         {QStringLiteral("borderless"), QStringLiteral("animation")});

        QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
        QVERIFY(!doc.isNull());
        QJsonObject obj = doc.object();
        QCOMPARE(obj[QLatin1String("apiVersion")].toInt(), 1);
    }

    void testRegisterBridge_storesBridgeName()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("6.0"), {});

        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
    }

    void testRegisterBridge_storesCapabilities()
    {
        QStringList caps = {QStringLiteral("borderless"), QStringLiteral("maximize"), QStringLiteral("animation")};
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("6.0"), caps);

        QCOMPARE(m_bridgeAdaptor->bridgeCapabilities(), caps);
    }

    void testRegisterBridge_emitsBridgeRegistered()
    {
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::bridgeRegistered);

        m_bridgeAdaptor->registerBridge(QStringLiteral("hyprland"), QStringLiteral("0.40"),
                                        {QStringLiteral("modifiers")});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("hyprland"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("0.40"));
    }

    // =====================================================================
    // CompositorBridgeAdaptor: hasCapability
    // =====================================================================

    void testHasCapability_registered_returnsTrue()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("6.0"), {QStringLiteral("borderless")});

        QVERIFY(m_bridgeAdaptor->hasCapability(QStringLiteral("borderless")));
    }

    void testHasCapability_notRegistered_returnsFalse()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("6.0"), {QStringLiteral("borderless")});

        QVERIFY(!m_bridgeAdaptor->hasCapability(QStringLiteral("unknown_capability")));
    }

    // =====================================================================
    // CompositorBridgeAdaptor: reportModifierState
    // =====================================================================

    void testReportModifierState_emitsSignal()
    {
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::modifierStateChanged);

        m_bridgeAdaptor->reportModifierState(0x04000000, 0x00000001); // Qt::ShiftModifier, Qt::LeftButton

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0x04000000);
        QCOMPARE(spy.at(0).at(1).toInt(), 0x00000001);
    }

    // =====================================================================
    // ControlAdaptor: getFullState
    // =====================================================================

    void testControlGetFullState_returnsValidJson()
    {
        QString stateJson = m_controlAdaptor->getFullState();
        QJsonDocument doc = QJsonDocument::fromJson(stateJson.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isObject());

        QJsonObject obj = doc.object();

        // Should have layouts array
        QVERIFY(obj.contains(QLatin1String("layouts")));
        QVERIFY(obj[QLatin1String("layouts")].isArray());

        // Should have windows array
        QVERIFY(obj.contains(QLatin1String("windows")));

        // Should have activeLayoutId
        QVERIFY(obj.contains(QLatin1String("activeLayoutId")));
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    CompositorBridgeAdaptor* m_bridgeAdaptor = nullptr;

    LayoutManager* m_layoutManager = nullptr;
    StubSettingsBridge* m_settings = nullptr;
    StubZoneDetectorBridge* m_zoneDetector = nullptr;
    QObject* m_wtaParent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    QObject* m_controlParent = nullptr;
    ControlAdaptor* m_controlAdaptor = nullptr;
};

QTEST_MAIN(TestCompositorBridge)
#include "test_compositor_bridge.moc"
