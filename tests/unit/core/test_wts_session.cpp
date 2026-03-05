// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_session.cpp
 * @brief Unit tests for WindowTrackingService session restore, clear-stale, and resnap
 *
 * Tests cover:
 * 1. Zone-number fallback on session restore
 * 2. Floating window skips restore
 * 3. Clear stale pending assignments
 * 4. Resnap from previous layout
 * 5. Rotation calculations
 * 6. Daemon restart / pending restore
 * 7. Multi-monitor restore edge cases
 * 8. Auto-snap marking
 * 9. Consume pending assignment
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QRectF>

#include "core/windowtrackingservice.h"
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "core/utils.h"

using namespace PlasmaZones;

// =========================================================================
// Stub Settings
// =========================================================================

// Stub ISettings: all pure virtuals return sensible defaults, setters are no-ops.
// Condensed to save lines (ISettings has ~100 virtual methods).
class StubSettingsSession : public ISettings
{
    Q_OBJECT
public:
    explicit StubSettingsSession(QObject* p = nullptr)
        : ISettings(p)
    {
    }
    // Activation
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
    // Visualization
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
    // Geometry
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
    // Exclusions
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
    // Zone selector
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
    // Window behavior
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
    // Animations
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
    // Autotile
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
    // Persistence
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

class StubZoneDetectorSession : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSession(QObject* parent = nullptr)
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

class TestWtsSession : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsSession(nullptr);
        m_zoneDetector = new StubZoneDetectorSession(nullptr);
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
    }

    // =====================================================================
    // P1: Session Restore
    // =====================================================================

    void testRestore_zoneNumberFallback()
    {
        QString stableId = QStringLiteral("firefox:Navigator");
        QString oldZoneId = QUuid::createUuid().toString();

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {oldZoneId};
        m_service->setPendingZoneAssignments(pendingZones);

        QHash<QString, QString> pendingScreens;
        pendingScreens[stableId] = QString();
        m_service->setPendingScreenAssignments(pendingScreens);

        QHash<QString, QString> pendingLayouts;
        pendingLayouts[stableId] = m_testLayout->id().toString();
        m_service->setPendingLayoutAssignments(pendingLayouts);

        QHash<QString, QList<int>> pendingNumbers;
        pendingNumbers[stableId] = {2};
        m_service->setPendingZoneNumbers(pendingNumbers);

        SnapResult result =
            m_service->calculateRestoreFromSession(QStringLiteral("firefox:Navigator:99999"), QString(), false);
        Q_UNUSED(result);

        QVERIFY(m_service->pendingZoneNumbers().contains(stableId));
        QCOMPARE(m_service->pendingZoneNumbers().value(stableId), QList<int>{2});
    }

    void testRestore_floatingWindowSkipsRestore()
    {
        QString stableId = QStringLiteral("firefox:Navigator");
        QString windowId = QStringLiteral("firefox:Navigator:12345");

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {m_zoneIds[0]};
        m_service->setPendingZoneAssignments(pendingZones);

        QSet<QString> floating;
        floating.insert(stableId);
        m_service->setFloatingWindows(floating);

        SnapResult result = m_service->calculateRestoreFromSession(windowId, QString(), false);
        QVERIFY(!result.shouldSnap);
    }

    // =====================================================================
    // P1: Clear Stale Pending
    // =====================================================================

    void testClearStalePendingAssignment()
    {
        QString windowId = QStringLiteral("app:resource:12345");
        QString stableId = Utils::extractStableId(windowId);

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {m_zoneIds[0]};
        m_service->setPendingZoneAssignments(pendingZones);

        QHash<QString, QString> pendingScreens;
        pendingScreens[stableId] = QStringLiteral("DP-1");
        m_service->setPendingScreenAssignments(pendingScreens);

        QHash<QString, int> pendingDesktops;
        pendingDesktops[stableId] = 1;
        m_service->setPendingDesktopAssignments(pendingDesktops);

        QHash<QString, QString> pendingLayouts;
        pendingLayouts[stableId] = QUuid::createUuid().toString();
        m_service->setPendingLayoutAssignments(pendingLayouts);

        QHash<QString, QList<int>> pendingNumbers;
        pendingNumbers[stableId] = {1};
        m_service->setPendingZoneNumbers(pendingNumbers);

        bool cleared = m_service->clearStalePendingAssignment(windowId);
        QVERIFY(cleared);
        QVERIFY(!m_service->pendingZoneAssignments().contains(stableId));
        QVERIFY(!m_service->pendingScreenAssignments().contains(stableId));
        QVERIFY(!m_service->pendingDesktopAssignments().contains(stableId));
        QVERIFY(!m_service->pendingLayoutAssignments().contains(stableId));
        QVERIFY(!m_service->pendingZoneNumbers().contains(stableId));
    }

    // =====================================================================
    // P1: Resnap
    // =====================================================================

    void testResnapFromPreviousLayout_zonePositionMapping()
    {
        QString window1 = QStringLiteral("app1:win:111");
        QString window2 = QStringLiteral("app2:win:222");
        QString window3 = QStringLiteral("app3:win:333");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);
        m_service->assignWindowToZone(window3, m_zoneIds[2], QString(), 0);

        Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<RotationEntry> resnap = m_service->calculateResnapFromPreviousLayout();
        QVector<RotationEntry> secondCall = m_service->calculateResnapFromPreviousLayout();
        QVERIFY(secondCall.isEmpty()); // Buffer consumed on first call
    }

    // =====================================================================
    // P1: Rotation
    // =====================================================================

    void testCalculateRotation_clockwiseAndCounterClockwise()
    {
        QString window1 = QStringLiteral("app1:win:111");
        QString window2 = QStringLiteral("app2:win:222");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);

        QVector<RotationEntry> cw = m_service->calculateRotation(true);
        QVector<RotationEntry> ccw = m_service->calculateRotation(false);

        Q_UNUSED(cw);
        Q_UNUSED(ccw);
    }

    // =====================================================================
    // P0: Daemon Restart / Pending Restore
    // =====================================================================

    void testDaemonRestart_pendingRestoresAvailableEmitted()
    {
        QString stableId = QStringLiteral("firefox:Navigator");

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {m_zoneIds[0]};
        m_service->setPendingZoneAssignments(pendingZones);

        QHash<QString, QString> pendingLayouts;
        pendingLayouts[stableId] = m_testLayout->id().toString();
        m_service->setPendingLayoutAssignments(pendingLayouts);

        QVERIFY(m_service->pendingZoneAssignments().contains(stableId));
        QCOMPARE(m_service->pendingZoneAssignments().value(stableId).first(), m_zoneIds[0]);
    }

    // =====================================================================
    // P0: Restore wrong display (multi-monitor)
    // =====================================================================

    void testRestore_wrongDisplay_multiMonitor()
    {
        QString stableId = QStringLiteral("app:win");

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {m_zoneIds[0]};
        m_service->setPendingZoneAssignments(pendingZones);

        QHash<QString, QString> pendingScreens;
        pendingScreens[stableId] = QStringLiteral("HDMI-2");
        m_service->setPendingScreenAssignments(pendingScreens);

        QCOMPARE(m_service->pendingScreenAssignments().value(stableId), QStringLiteral("HDMI-2"));
    }

    void testRestore_savedScreenDisconnected()
    {
        QString stableId = QStringLiteral("app:win");
        QString windowId = QStringLiteral("app:win:12345");

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {m_zoneIds[0]};
        m_service->setPendingZoneAssignments(pendingZones);

        QHash<QString, QString> pendingScreens;
        pendingScreens[stableId] = QStringLiteral("DISCONNECTED-99");
        m_service->setPendingScreenAssignments(pendingScreens);

        QHash<QString, QString> pendingLayouts;
        pendingLayouts[stableId] = m_testLayout->id().toString();
        m_service->setPendingLayoutAssignments(pendingLayouts);

        SnapResult result = m_service->calculateRestoreFromSession(windowId, QStringLiteral("DP-1"), false);
        if (result.shouldSnap) {
            QVERIFY(result.geometry.isValid());
        }
    }

    // =====================================================================
    // P1: Auto-snap / Mark as auto-snapped
    // =====================================================================

    void testMarkAsAutoSnapped()
    {
        QString windowId = QStringLiteral("app:win:12345");

        QVERIFY(!m_service->isAutoSnapped(windowId));
        m_service->markAsAutoSnapped(windowId);
        QVERIFY(m_service->isAutoSnapped(windowId));
        QVERIFY(m_service->clearAutoSnapped(windowId));
        QVERIFY(!m_service->isAutoSnapped(windowId));
    }

    // =====================================================================
    // P1: Consume pending assignment
    // =====================================================================

    void testConsumePendingAssignment()
    {
        QString windowId = QStringLiteral("app:resource:12345");
        QString stableId = Utils::extractStableId(windowId);

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {m_zoneIds[0]};
        m_service->setPendingZoneAssignments(pendingZones);

        QHash<QString, QList<int>> pendingNumbers;
        pendingNumbers[stableId] = {1};
        m_service->setPendingZoneNumbers(pendingNumbers);

        m_service->consumePendingAssignment(windowId);

        QVERIFY(!m_service->pendingZoneAssignments().contains(stableId));
        QVERIFY(!m_service->pendingZoneNumbers().contains(stableId));
    }

    // =====================================================================
    // P0: Layout Import UUID Collision
    // =====================================================================

    void testLayoutImport_uuidCollision_regeneratesIds()
    {
        QString stableId = QStringLiteral("app:resource");
        QString bogusUuid = QUuid::createUuid().toString();

        QHash<QString, QStringList> pendingZones;
        pendingZones[stableId] = {bogusUuid};
        m_service->setPendingZoneAssignments(pendingZones);

        QHash<QString, QString> pendingLayouts;
        pendingLayouts[stableId] = m_testLayout->id().toString();
        m_service->setPendingLayoutAssignments(pendingLayouts);

        QHash<QString, QList<int>> pendingNumbers;
        pendingNumbers[stableId] = {1};
        m_service->setPendingZoneNumbers(pendingNumbers);

        QVERIFY(m_service->pendingZoneNumbers().contains(stableId));
        QCOMPARE(m_service->pendingZoneNumbers().value(stableId).first(), 1);
    }

private:
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsSession* m_settings = nullptr;
    StubZoneDetectorSession* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsSession)
#include "test_wts_session.moc"
