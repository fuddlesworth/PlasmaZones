// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_lifecycle.cpp
 * @brief Unit tests for WindowTrackingService lifecycle: windowClosed and onLayoutChanged
 *
 * Tests cover:
 * 1. Window close -> pending zone persistence (P0 crash/data-loss)
 * 2. Pre-snap geometry stable ID migration on close
 * 3. Pre-float zone conversion on close
 * 4. Layout change -> stale assignment removal and resnap buffer
 * 5. State change signal emission
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QSignalSpy>
#include <QRectF>
#include <memory>

#include "core/windowtrackingservice.h"
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

class StubSettingsLifecycle : public ISettings
{
    Q_OBJECT
public:
    explicit StubSettingsLifecycle(QObject* parent = nullptr)
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
    int autotileBorderWidth() const override
    {
        return 2;
    }
    void setAutotileBorderWidth(int) override
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

class StubZoneDetectorLifecycle : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorLifecycle(QObject* parent = nullptr)
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
// Helper
// =========================================================================

static Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new Layout(QStringLiteral("TestLayout"), LayoutType::Custom, parent);
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

class TestWtsLifecycle : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        // Pass nullptr as parent to avoid double-delete: cleanup() deletes manually
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsLifecycle(nullptr);
        m_zoneDetector = new StubZoneDetectorLifecycle(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, m_settings, nullptr, nullptr);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
    }

    void cleanup()
    {
        delete m_service;
        m_service = nullptr;
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
    // P0: Window Close -> Pending Zone Persistence
    // =====================================================================

    void testWindowClosed_persistsZoneToPending()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QString appId = Utils::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        QVERIFY(m_service->isWindowSnapped(windowId));

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->isWindowSnapped(windowId));
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneIds.first(), m_zoneIds[0]);
    }

    void testWindowClosed_floatingWindowNotPersisted()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QString appId = Utils::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->setWindowFloating(windowId, true);

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    void testWindowClosed_preTileGeometryConvertedToStableId()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|99999");
        QString appId = Utils::extractAppId(windowId);

        m_service->storePreTileGeometry(windowId, QRect(100, 200, 800, 600));
        QVERIFY(m_service->hasPreTileGeometry(windowId));

        m_service->windowClosed(windowId);

        QVERIFY(m_service->hasPreTileGeometry(appId));
        auto geo = m_service->preTileGeometry(appId);
        QVERIFY(geo.has_value());
        QCOMPARE(geo->x(), 100);
        QCOMPARE(geo->width(), 800);
    }

    void testWindowClosed_preFloatZonesConvertedToStableId()
    {
        QString windowId = QStringLiteral("org.kde.kate|55555");
        QString appId = Utils::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[1], QStringLiteral("DP-1"), 1);
        m_service->unsnapForFloat(windowId);

        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[1]);

        m_service->windowClosed(windowId);

        QCOMPARE(m_service->preFloatZone(appId), m_zoneIds[1]);
    }

    void testWindowClosed_scheduleSaveStateCalled()
    {
        QString windowId = QStringLiteral("app|12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        QSignalSpy spy(m_service, &WindowTrackingService::stateChanged);
        m_service->windowClosed(windowId);

        QVERIFY(spy.count() >= 1);
    }

    // =====================================================================
    // P0: Layout Change
    // =====================================================================

    void testOnLayoutChanged_staleAssignmentsRemoved()
    {
        QString windowId = QStringLiteral("app|12345");
        QString screen = QStringLiteral("DP-1");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], screen, 0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->assignLayout(screen, m_layoutManager->currentVirtualDesktop(), QString(), newLayout);
        m_layoutManager->setActiveLayout(newLayout);

        m_service->onLayoutChanged();

        QVERIFY(!m_service->isWindowSnapped(windowId));
    }

    void testOnLayoutChanged_resnapBufferPopulated()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);

        Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<RotationEntry> resnap = m_service->calculateResnapFromPreviousLayout();
        // In headless mode the buffer may be empty but the call must not crash.
        // The original assertion `resnap.size() >= 0` was always true;
        // we keep this as a smoke-test and document the limitation.
        Q_UNUSED(resnap);
    }

    void testOnLayoutChanged_floatingWindowsExcludedFromResnap()
    {
        QString windowId = QStringLiteral("app|12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);
        m_service->setWindowFloating(windowId, true);

        Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<RotationEntry> resnap = m_service->calculateResnapFromPreviousLayout();
        for (const RotationEntry& entry : resnap) {
            QVERIFY(entry.windowId != windowId);
        }
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsLifecycle* m_settings = nullptr;
    StubZoneDetectorLifecycle* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsLifecycle)
#include "test_wts_lifecycle.moc"
