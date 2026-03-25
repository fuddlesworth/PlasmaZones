// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_assist_virtual.cpp
 * @brief Unit tests for snap assist behavior with virtual screen IDs
 *
 * Tests cover:
 * 1. screensMatch() with virtual screen IDs (same, different, physical vs virtual)
 * 2. buildOccupiedZoneSet() filtering with virtual screen IDs
 * 3. getEmptyZonesJson() with virtual screen IDs (returns valid JSON)
 *
 * These tests reproduce the snap assist bug where virtual screens cause
 * screensMatch() to return false even when screen IDs should match,
 * making all zones appear empty/occupied incorrectly.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

#include "core/windowtrackingservice.h"
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "core/virtualscreen.h"
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings (minimal, same pattern as test_wts_queries.cpp)
// =========================================================================

class StubSettingsSnapAssist : public ISettings
{
    Q_OBJECT
public:
    explicit StubSettingsSnapAssist(QObject* parent = nullptr)
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
        return true;
    }
    void setSnapAssistFeatureEnabled(bool) override
    {
    }
    bool snapAssistEnabled() const override
    {
        return true;
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

class StubZoneDetectorSnapAssist : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSnapAssist(QObject* parent = nullptr)
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

class TestSnapAssistVirtual : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsSnapAssist(nullptr);
        m_zoneDetector = new StubZoneDetectorSnapAssist(nullptr);
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
    // screensMatch — Virtual Screen ID scenarios
    // =====================================================================

    void testScreensMatch_identicalVirtualIds_returnsTrue()
    {
        // Two identical virtual screen IDs must match (fast path: a == b)
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QVERIFY(Utils::screensMatch(vsId, vsId));
    }

    void testScreensMatch_differentVirtualIndexes_returnsFalse()
    {
        // Two different virtual screens on the same physical monitor must NOT match
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QVERIFY(!Utils::screensMatch(vs0, vs1));
    }

    void testScreensMatch_physicalVsVirtual_returnsFalse()
    {
        // A physical screen ID vs a virtual screen ID derived from it must NOT match
        // (once virtual screens are configured, the physical ID is no longer a valid screen)
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QVERIFY(!Utils::screensMatch(physId, vsId));
        QVERIFY(!Utils::screensMatch(vsId, physId));
    }

    void testScreensMatch_differentPhysicalVirtual_returnsFalse()
    {
        // Virtual screens from different physical monitors must NOT match
        QString vsA = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vsB = QStringLiteral("LG:27GL850:ABC123/vs:0");
        QVERIFY(!Utils::screensMatch(vsA, vsB));
    }

    void testScreensMatch_emptyVsVirtual_returnsFalse()
    {
        // Empty string vs virtual screen ID must NOT match
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QVERIFY(!Utils::screensMatch(QString(), vsId));
        QVERIFY(!Utils::screensMatch(vsId, QString()));
    }

    void testScreensMatch_bothEmpty_returnsTrue()
    {
        // Two empty strings are identical -> true via fast path
        QVERIFY(Utils::screensMatch(QString(), QString()));
    }

    void testScreensMatch_identicalPhysicalIds_returnsTrue()
    {
        // Sanity: two identical physical IDs still match
        // (In headless mode there are no QScreen objects, so findScreenByIdOrName
        // returns nullptr for both sides; screensMatch returns false when both
        // are non-virtual and neither resolves to a QScreen. The fast path a==b
        // handles the identical case.)
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QVERIFY(Utils::screensMatch(physId, physId));
    }

    // =====================================================================
    // buildOccupiedZoneSet — Virtual Screen filtering
    // =====================================================================

    void testBuildOccupiedZoneSet_sameVirtualScreen_includesWindow()
    {
        // Window snapped on vs:0, query for vs:0 -> zone should appear occupied
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString windowId = QStringLiteral("konsole|aaa-bbb-ccc");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], vsId, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vsId);
        QUuid expectedZoneUuid = m_testLayout->zones().at(0)->id();
        QVERIFY2(occupied.contains(expectedZoneUuid), "Zone should be occupied when querying same virtual screen");
    }

    void testBuildOccupiedZoneSet_differentVirtualScreen_excludesWindow()
    {
        // Window snapped on vs:0, query for vs:1 -> zone should NOT appear occupied
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QString windowId = QStringLiteral("konsole|aaa-bbb-ccc");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vs1);
        QUuid zoneUuid = m_testLayout->zones().at(0)->id();
        QVERIFY2(!occupied.contains(zoneUuid), "Zone should NOT be occupied when querying different virtual screen");
    }

    void testBuildOccupiedZoneSet_physicalQueryVirtualWindow_excludesWindow()
    {
        // Window snapped on virtual screen, query with physical ID -> should NOT match
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QString windowId = QStringLiteral("dolphin|ddd-eee-fff");

        m_service->assignWindowToZone(windowId, m_zoneIds[1], vsId, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(physId);
        QUuid zoneUuid = m_testLayout->zones().at(1)->id();
        QVERIFY2(!occupied.contains(zoneUuid), "Physical ID query should not match windows on virtual screens");
    }

    void testBuildOccupiedZoneSet_virtualQueryPhysicalWindow_excludesWindow()
    {
        // Window snapped on physical screen, query with virtual ID -> should NOT match
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString windowId = QStringLiteral("kate|ggg-hhh-iii");

        m_service->assignWindowToZone(windowId, m_zoneIds[2], physId, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vsId);
        QUuid zoneUuid = m_testLayout->zones().at(2)->id();
        QVERIFY2(!occupied.contains(zoneUuid), "Virtual ID query should not match windows on physical screens");
    }

    void testBuildOccupiedZoneSet_emptyFilter_includesAllWindows()
    {
        // Empty screen filter -> all windows should appear occupied regardless of screen
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vs0, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vs1, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(QString());
        QCOMPARE(occupied.size(), 2);
    }

    void testBuildOccupiedZoneSet_floatingWindowExcluded_virtualScreen()
    {
        // Floating window on virtual screen should be excluded from occupied set
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vsId, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vsId, 1);
        m_service->setWindowFloating(win1, true);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vsId);

        QUuid zone0Uuid = m_testLayout->zones().at(0)->id();
        QUuid zone1Uuid = m_testLayout->zones().at(1)->id();
        QVERIFY2(!occupied.contains(zone0Uuid), "Floating window's zone should not appear occupied");
        QVERIFY2(occupied.contains(zone1Uuid), "Non-floating window's zone should appear occupied");
    }

    // =====================================================================
    // getEmptyZonesJson — Virtual Screen scenarios
    // =====================================================================

    void testGetEmptyZonesJson_virtualScreen_returnsValidJson()
    {
        // getEmptyZonesJson with virtual screen ID should return valid JSON array
        // (may be empty in headless mode since there's no QScreen, but must not crash
        // and must return valid JSON)
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");

        QString result = m_service->getEmptyZonesJson(vsId);

        // Must be parseable JSON
        QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
        QVERIFY2(!doc.isNull(), qPrintable(QStringLiteral("getEmptyZonesJson returned invalid JSON: ") + result));
        QVERIFY2(doc.isArray(), qPrintable(QStringLiteral("getEmptyZonesJson should return array: ") + result));
    }

    void testGetEmptyZonesJson_emptyScreenId_returnsValidJson()
    {
        // Empty screen ID fallback should also produce valid JSON
        QString result = m_service->getEmptyZonesJson(QString());

        QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
        QVERIFY2(!doc.isNull(), "getEmptyZonesJson with empty screen should return valid JSON");
        QVERIFY(doc.isArray());
    }

    // =====================================================================
    // VirtualScreenId utilities (sanity checks for test infrastructure)
    // =====================================================================

    void testVirtualScreenId_isVirtual()
    {
        QVERIFY(VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107/vs:0")));
        QVERIFY(!VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107")));
    }

    void testVirtualScreenId_extractPhysicalId()
    {
        QCOMPARE(VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107/vs:0")),
                 QStringLiteral("Dell:U2722D:115107"));
        // Non-virtual ID returns itself
        QCOMPARE(VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107")),
                 QStringLiteral("Dell:U2722D:115107"));
    }

    void testVirtualScreenId_extractIndex()
    {
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:0")), 0);
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:3")), 3);
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107")), -1);
    }

    void testVirtualScreenId_make()
    {
        QCOMPARE(VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 0),
                 QStringLiteral("Dell:U2722D:115107/vs:0"));
        QCOMPARE(VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 2),
                 QStringLiteral("Dell:U2722D:115107/vs:2"));
    }

    // =====================================================================
    // screensMatch — Dead code detection
    // The current implementation extracts physA/physB but never uses them.
    // These tests document the current (potentially buggy) behavior and
    // will need updating when the dead code is fixed.
    // =====================================================================

    void testScreensMatch_deadCode_physicalIdsExtractedButNotCompared()
    {
        // This test documents the dead code in screensMatch():
        // When one or both IDs are virtual, physA and physB are extracted
        // but the function unconditionally returns false (after the a==b check).
        //
        // The fix would be to use physA/physB for meaningful comparison,
        // but the correct behavior depends on product requirements.
        // For now, just verify the function doesn't crash and returns false
        // for non-identical virtual IDs.
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        // Both are virtual, different index -> should be false (correct behavior)
        QVERIFY(!Utils::screensMatch(vs0, vs1));

        // Physical parent vs virtual child -> should be false (correct behavior for
        // the "virtual screens are separate screens" model)
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QVERIFY(!Utils::screensMatch(physId, vs0));
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsSnapAssist* m_settings = nullptr;
    StubZoneDetectorSnapAssist* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestSnapAssistVirtual)
#include "test_snap_assist_virtual.moc"
