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

#include <PhosphorProtocol/ServiceConstants.h>
#include "dbus/controladaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "helpers/StubSettings.h"

using StubSettingsBridge = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

// WindowTrackingAdaptor only null-checks the detector and SnapEngine only
// stores it, so the shared inert stub covers both. The former local copy
// answered layout() with nullptr instead of remembering the set layout; no
// caller in this suite reads it either way.
#include "helpers/StubZoneDetector.h"

using StubZoneDetectorBridge = PlasmaZones::StubZoneDetector;

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

        // For ControlAdaptor tests we need a WTA + PhosphorZones::LayoutRegistry
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsBridge(nullptr);
        m_zoneDetector = new StubZoneDetectorBridge(nullptr);

        m_wtaParent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr,
                                          m_wtaParent);

        m_snapEngine = new SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        m_wta->setEngines(m_snapEngine, nullptr, nullptr);

        // Create a test layout so getFullState has data
        auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), m_layoutManager);
        auto* zone = new PhosphorZones::Zone(layout);
        zone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        zone->setZoneNumber(1);
        layout->addZone(zone);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        m_controlParent = new QObject(nullptr);
        m_controlAdaptor = new ControlAdaptor(m_wta, nullptr, nullptr, m_layoutManager, nullptr, nullptr,
                                              m_bridgeAdaptor, m_controlParent);
    }

    void cleanup()
    {
        delete m_controlParent;
        m_controlParent = nullptr;
        m_controlAdaptor = nullptr;
        // Detach BOTH borrowed pointers before the engine dies so the service
        // never holds a dangling SnapEngine* (same discipline as
        // wta_convenience_fixture.h).
        m_wta->service()->setSnapState(nullptr);
        m_wta->service()->setSnapEngine(nullptr);
        delete m_snapEngine;
        m_snapEngine = nullptr;
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
        // A NEWER peer version: the reply must carry the DAEMON's own
        // version, so passing ApiVersion here would make the assertion a
        // tautological echo check.
        PhosphorProtocol::BridgeRegistrationResult result = m_bridgeAdaptor->registerBridge(
            QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion + 1),
            {QStringLiteral("borderless"), QStringLiteral("animation")});

        // The reply must be an ACCEPT carrying the daemon's own version —
        // the reject paths also echo it, so acceptance is asserted too.
        QVERIFY(result.sessionId != QStringLiteral("REJECTED"));
        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
        QCOMPARE(result.apiVersion, QString::number(PhosphorProtocol::Service::ApiVersion));
    }

    void testRegisterBridge_rejectsEmptyName()
    {
        // Input-validation boundary: an empty compositorName would commit a
        // registration isBridgeRegistered() can never observe. Live-peer
        // pattern (same as the old-version reject): the gate must also not
        // CLOBBER an existing registration through the replacement path.
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::bridgeRegistered);
        const PhosphorProtocol::BridgeRegistrationResult live = m_bridgeAdaptor->registerBridge(
            QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
            {QStringLiteral("borderless")});
        QVERIFY(live.sessionId != QStringLiteral("REJECTED"));
        QCOMPARE(spy.count(), 1);

        PhosphorProtocol::BridgeRegistrationResult result =
            m_bridgeAdaptor->registerBridge(QString(), QString::number(PhosphorProtocol::Service::ApiVersion), {});
        QCOMPARE(result.sessionId, QStringLiteral("REJECTED"));
        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
        QCOMPARE(spy.count(), 1);
    }

    void testRegisterBridge_storesBridgeName()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
                                        {});

        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
    }

    void testRegisterBridge_storesCapabilities()
    {
        QStringList caps = {QStringLiteral("borderless"), QStringLiteral("maximize"), QStringLiteral("animation")};
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
                                        caps);

        QCOMPARE(m_bridgeAdaptor->bridgeCapabilities(), caps);
    }

    void testRegisterBridge_emitsBridgeRegistered()
    {
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::bridgeRegistered);

        m_bridgeAdaptor->registerBridge(QStringLiteral("hyprland"),
                                        QString::number(PhosphorProtocol::Service::ApiVersion),
                                        {QStringLiteral("modifiers")});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("hyprland"));
        QCOMPARE(spy.at(0).at(1).toString(), QString::number(PhosphorProtocol::Service::ApiVersion));
    }

    // Version gate regression test: a peer speaking an older protocol
    // version (< MinPeerApiVersion) must be rejected with the REJECTED
    // sentinel in sessionId, must NOT update the stored bridge name, and
    // must NOT emit bridgeRegistered. If this regresses, stale effects
    // would silently connect and either crash on marshalling mismatches or
    // hear nothing on a renamed surface. The peer string is derived as
    // MinPeerApiVersion - 1, so the test pins the CURRENT floor across
    // every future bump.
    void testRegisterBridge_rejectsOldVersion()
    {
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::bridgeRegistered);

        // Register a VALID peer first: the real hazard is a stale v(N-1)
        // effect clobbering a live registration through the replacement
        // path that sits right after the version gates — a fresh-adaptor
        // "name still empty" assert cannot distinguish "gate fired" from
        // "call did nothing".
        const PhosphorProtocol::BridgeRegistrationResult live = m_bridgeAdaptor->registerBridge(
            QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
            {QStringLiteral("borderless")});
        QVERIFY(live.sessionId != QStringLiteral("REJECTED"));
        QCOMPARE(spy.count(), 1);

        // One below the CURRENT floor, so every future MinPeerApiVersion
        // bump keeps this test pinning the just-outdated peer instead of an
        // anciently-rejected one.
        PhosphorProtocol::BridgeRegistrationResult result = m_bridgeAdaptor->registerBridge(
            QStringLiteral("stale-kwin"), QString::number(PhosphorProtocol::Service::MinPeerApiVersion - 1),
            {QStringLiteral("shadow")});

        QCOMPARE(result.sessionId, QStringLiteral("REJECTED"));
        QCOMPARE(result.apiVersion, QString::number(PhosphorProtocol::Service::ApiVersion));
        QCOMPARE(spy.count(), 1); // only the live registration announced
        // The live registration survives untouched.
        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
        QVERIFY(m_bridgeAdaptor->hasCapability(QStringLiteral("borderless")));
        QVERIFY(!m_bridgeAdaptor->hasCapability(QStringLiteral("shadow")));
    }

    // Non-numeric versions parse as 0 via QString::toInt(), which is
    // below MinPeerApiVersion and must also be rejected.
    void testRegisterBridge_rejectsNonNumericVersion()
    {
        QSignalSpy spy(m_bridgeAdaptor, &CompositorBridgeAdaptor::bridgeRegistered);
        const PhosphorProtocol::BridgeRegistrationResult live = m_bridgeAdaptor->registerBridge(
            QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion), {});
        QVERIFY(live.sessionId != QStringLiteral("REJECTED"));
        QCOMPARE(spy.count(), 1);

        PhosphorProtocol::BridgeRegistrationResult result =
            m_bridgeAdaptor->registerBridge(QStringLiteral("weird-compositor"), QStringLiteral("garbage"), {});

        QCOMPARE(result.sessionId, QStringLiteral("REJECTED"));
        QCOMPARE(result.apiVersion, QString::number(PhosphorProtocol::Service::ApiVersion));
        QCOMPARE(spy.count(), 1); // no clobber, no new announce
        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
    }

    // Accept boundary at EXACTLY MinPeerApiVersion: covered only
    // coincidentally while ApiVersion == MinPeerApiVersion; the first
    // ApiVersion bump past the floor would otherwise leave an off-by-one
    // `<=` in the gate unpinned.
    void testRegisterBridge_acceptsAtExactFloor()
    {
        PhosphorProtocol::BridgeRegistrationResult result = m_bridgeAdaptor->registerBridge(
            QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::MinPeerApiVersion), {});
        QVERIFY(result.sessionId != QStringLiteral("REJECTED"));
        QCOMPARE(m_bridgeAdaptor->bridgeName(), QStringLiteral("kwin"));
    }

    // =====================================================================
    // CompositorBridgeAdaptor: hasCapability
    // =====================================================================

    void testHasCapability_registered_returnsTrue()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
                                        {QStringLiteral("borderless")});

        QVERIFY(m_bridgeAdaptor->hasCapability(QStringLiteral("borderless")));
    }

    void testHasCapability_notRegistered_returnsFalse()
    {
        m_bridgeAdaptor->registerBridge(QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
                                        {QStringLiteral("borderless")});

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

        // Should have layouts array, and the layout seeded in init() must surface.
        QVERIFY(obj.contains(QLatin1String("layouts")));
        QVERIFY(obj[QLatin1String("layouts")].isArray());
        QVERIFY(obj[QLatin1String("layouts")].toArray().size() >= 1);

        // Should have windows array
        QVERIFY(obj.contains(QLatin1String("windows")));
        QVERIFY(obj[QLatin1String("windows")].isArray());

        // Should have activeLayoutId, non-empty because init() set an active layout.
        QVERIFY(obj.contains(QLatin1String("activeLayoutId")));
        QVERIFY(!obj[QLatin1String("activeLayoutId")].toString().isEmpty());
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    CompositorBridgeAdaptor* m_bridgeAdaptor = nullptr;

    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsBridge* m_settings = nullptr;
    StubZoneDetectorBridge* m_zoneDetector = nullptr;
    QObject* m_wtaParent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    QObject* m_controlParent = nullptr;
    ControlAdaptor* m_controlAdaptor = nullptr;
};

QTEST_MAIN(TestCompositorBridge)
#include "test_compositor_bridge.moc"
