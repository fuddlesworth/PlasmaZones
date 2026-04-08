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
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsBridge = StubSettings;

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
