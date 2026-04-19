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
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsBridge = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

class StubZoneDetectorBridge : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorBridge(QObject* parent = nullptr)
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
        m_layoutManager = makePzLayoutManager(nullptr).release();
        m_settings = new StubSettingsBridge(nullptr);
        m_zoneDetector = new StubZoneDetectorBridge(nullptr);

        m_wtaParent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, m_wtaParent);

        // Create a test layout so getFullState has data
        auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), m_layoutManager);
        auto* zone = new PhosphorZones::Zone(layout);
        zone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        zone->setZoneNumber(1);
        layout->addZone(zone);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        m_controlParent = new QObject(nullptr);
        m_controlAdaptor = new ControlAdaptor(m_wta, nullptr, m_layoutManager, nullptr, nullptr, m_controlParent);
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
        BridgeRegistrationResult result = m_bridgeAdaptor->registerBridge(
            QStringLiteral("kwin"), QStringLiteral("2"), {QStringLiteral("borderless"), QStringLiteral("animation")});

        QCOMPARE(result.apiVersion, QStringLiteral("2"));
    }

    void testRegisterBridge_storesBridgeName()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("2"), {});

        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
    }

    void testRegisterBridge_storesCapabilities()
    {
        QStringList caps = {QStringLiteral("borderless"), QStringLiteral("maximize"), QStringLiteral("animation")};
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("2"), caps);

        QCOMPARE(m_bridgeAdaptor->bridgeCapabilities(), caps);
    }

    void testRegisterBridge_emitsBridgeRegistered()
    {
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::bridgeRegistered);

        m_bridgeAdaptor->registerBridge(QStringLiteral("hyprland"), QStringLiteral("2"), {QStringLiteral("modifiers")});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("hyprland"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("2"));
    }

    // Version gate regression test: a peer speaking an older protocol
    // version (< MinPeerApiVersion) must be rejected with the REJECTED
    // sentinel in sessionId, must NOT update the stored bridge name, and
    // must NOT emit bridgeRegistered. If this regresses, stale effects
    // would silently connect and crash on marshalling mismatches.
    void testRegisterBridge_rejectsOldVersion()
    {
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::bridgeRegistered);

        BridgeRegistrationResult result = m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("1"),
                                                                          {QStringLiteral("borderless")});

        QCOMPARE(result.sessionId, QStringLiteral("REJECTED"));
        QCOMPARE(result.apiVersion, QStringLiteral("2"));
        QCOMPARE(spy.count(), 0);
        QVERIFY(m_bridgeAdaptor->bridgeName().isEmpty());
    }

    // Non-numeric versions parse as 0 via QString::toInt(), which is
    // below MinPeerApiVersion and must also be rejected.
    void testRegisterBridge_rejectsNonNumericVersion()
    {
        BridgeRegistrationResult result =
            m_bridgeAdaptor->registerBridge(QStringLiteral("weird-compositor"), QStringLiteral("garbage"), {});

        QCOMPARE(result.sessionId, QStringLiteral("REJECTED"));
        QVERIFY(m_bridgeAdaptor->bridgeName().isEmpty());
    }

    // =====================================================================
    // CompositorBridgeAdaptor: hasCapability
    // =====================================================================

    void testHasCapability_registered_returnsTrue()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("2"), {QStringLiteral("borderless")});

        QVERIFY(m_bridgeAdaptor->hasCapability(QStringLiteral("borderless")));
    }

    void testHasCapability_notRegistered_returnsFalse()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QStringLiteral("2"), {QStringLiteral("borderless")});

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
