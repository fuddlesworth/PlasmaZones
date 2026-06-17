// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_reactive_metadata.cpp
 * @brief Regression tests for the reactive metadataChanged path.
 *
 * WindowTrackingAdaptor subscribes to WindowRegistry::metadataChanged in
 * setWindowRegistry(). Per feedback_class_change_exclusion.md the handler
 * must update internal class tracking but NEVER retroactively unsnap,
 * re-snap, or re-evaluate rules on committed state.
 *
 * Concretely: lastUsedZoneClass is a human-readable tag stamped on the
 * last-used-zone tracking when a user snaps a window. If that window later
 * renames itself (Electron/CEF apps like Emby), the tag must refresh to
 * the new class so the next auto-snap-by-class check matches against the
 * live value — BUT the window's own zone assignment stays put, because
 * re-evaluating rules on a committed window would surprise users.
 */

#include <QCoreApplication>
#include <QRectF>
#include <QSignalSpy>
#include <QTest>
#include <memory>

#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorWindowRule/WindowRuleStore.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorZones/Zone.h>
#include "dbus/windowtrackingadaptor.h"

#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUuid>

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// ─────────────────────────────────────────────────────────────────────────
// Minimal zone-detector stub — the reactive tests don't exercise detection
// ─────────────────────────────────────────────────────────────────────────

class StubZoneDetectorReactive : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorReactive(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(PhosphorZones::Layout* layout) override
    {
        m_layout = layout;
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

private:
    PhosphorZones::Layout* m_layout = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────
// Fixture helpers
// ─────────────────────────────────────────────────────────────────────────

static PhosphorZones::Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new PhosphorZones::Zone(layout);
        const qreal x = static_cast<qreal>(i) / zoneCount;
        const qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

class TestWtaReactiveMetadata : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetectorReactive(nullptr);
        m_registry = new PhosphorEngine::WindowRegistry(nullptr);

        m_parent = new QObject(nullptr);
        m_wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, m_parent);
        m_wta->setWindowRegistry(m_registry);

        m_snapEngine = new SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        m_wta->setEngines(m_snapEngine, nullptr);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
        m_screenId = QStringLiteral("DP-1");
    }

    void cleanup()
    {
        m_wta->service()->setSnapState(nullptr);
        delete m_snapEngine;
        m_snapEngine = nullptr;

        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;

        delete m_registry;
        m_registry = nullptr;
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

    // ────────────────────────────────────────────────────────────────────
    // The substantive test — Emby scenario end-to-end through the adaptor
    // ────────────────────────────────────────────────────────────────────

    void lastUsedZoneClass_retagsOnClassMutation()
    {
        PhosphorPlacement::WindowTrackingService* service = m_wta->service();
        QVERIFY(service);

        const QString instanceId = QStringLiteral("cef1ba31-3316-4f05-84f5-ef627674b504");
        const QString classA = QStringLiteral("emby-beta");
        const QString classB = QStringLiteral("media.emby.client.beta");

        // 1. Effect registers the window's initial metadata before snapping.
        m_registry->upsert(instanceId, {classA, QString(), QString()});

        // 2. User snap: WTA's snapped handler stamps the live class onto
        //    m_lastUsedZoneClass via updateLastUsedZone. We call
        //    updateLastUsedZone directly since windowSnapped has extra moving
        //    parts (screen resolution, etc.) that aren't relevant here.
        service->updateLastUsedZone(m_zoneIds[0], m_screenId, classA, 1);
        QCOMPARE(service->lastUsedZoneClass(), classA);
        QCOMPARE(service->lastUsedZoneId(), m_zoneIds[0]);

        // Also give the service an assignment so we can verify committed
        // state is preserved across the metadata update.
        service->assignWindowToZone(instanceId, m_zoneIds[0], m_screenId, 1);
        QVERIFY(service->isWindowSnapped(instanceId));

        // 3. KWin rebroadcasts the window with a new class. Registry fires
        //    metadataChanged, WTA's lambda must (a) retag the class tracking
        //    and (b) leave the assignment alone.
        m_registry->upsert(instanceId, {classB, QString(), QString()});
        QCoreApplication::processEvents();

        // Class tag is updated.
        QCOMPARE(service->lastUsedZoneClass(), classB);
        // PhosphorZones::Zone id unchanged — NOT a retroactive move.
        QCOMPARE(service->lastUsedZoneId(), m_zoneIds[0]);
        // Committed snap state is preserved per feedback_class_change_exclusion.md.
        QVERIFY(service->isWindowSnapped(instanceId));
        QCOMPARE(service->zoneForWindow(instanceId), m_zoneIds[0]);
    }

    // ────────────────────────────────────────────────────────────────────
    // Guard: when another live window still owns the old class, the tag
    // must NOT change. Otherwise a second-instance rename would silently
    // corrupt the last-used-zone tracking for the first instance.
    // ────────────────────────────────────────────────────────────────────

    void lastUsedZoneClass_preservedWhenOtherInstancesOwnOldClass()
    {
        PhosphorPlacement::WindowTrackingService* service = m_wta->service();

        const QString instanceA = QStringLiteral("uuid-A");
        const QString instanceB = QStringLiteral("uuid-B");
        const QString classA = QStringLiteral("firefox");
        const QString classB = QStringLiteral("firefox-nightly");

        // Two live instances share the same class; last-used-zone is
        // stamped with that class.
        m_registry->upsert(instanceA, {classA, QString(), QString()});
        m_registry->upsert(instanceB, {classA, QString(), QString()});
        service->updateLastUsedZone(m_zoneIds[1], m_screenId, classA, 1);
        QCOMPARE(service->lastUsedZoneClass(), classA);

        // Instance B renames. Instance A still owns classA, so the tag
        // must remain classA — it's still meaningful for the surviving
        // instance(s).
        m_registry->upsert(instanceB, {classB, QString(), QString()});
        QCoreApplication::processEvents();

        QCOMPARE(service->lastUsedZoneClass(), classA);
    }

    // ────────────────────────────────────────────────────────────────────
    // Guard: if the renamed instance didn't set the tag, nothing should
    // happen. The retag is targeted — generic renames are no-ops.
    // ────────────────────────────────────────────────────────────────────

    void lastUsedZoneClass_unaffectedByUnrelatedRename()
    {
        PhosphorPlacement::WindowTrackingService* service = m_wta->service();

        m_registry->upsert(QStringLiteral("uuid-firefox"), {QStringLiteral("firefox"), QString(), QString()});
        m_registry->upsert(QStringLiteral("uuid-kate"), {QStringLiteral("kate"), QString(), QString()});

        service->updateLastUsedZone(m_zoneIds[2], m_screenId, QStringLiteral("firefox"), 1);

        // kate renames to kate-beta — firefox tag is untouched.
        m_registry->upsert(QStringLiteral("uuid-kate"), {QStringLiteral("kate-beta"), QString(), QString()});
        QCoreApplication::processEvents();

        QCOMPARE(service->lastUsedZoneClass(), QStringLiteral("firefox"));
    }

    // ────────────────────────────────────────────────────────────────────
    // RestorePosition rule override resolves through the composite windowId.
    //
    // Regression: shouldRestoreFloatedPosition must extract the BARE instance
    // id from the composite `appId|instanceId` before the WindowRegistry lookup
    // (the registry is keyed by instance id). Looking up by the composite id
    // always misses, which would silently disable every RestorePosition rule and
    // collapse the feature to the global setting.
    // ────────────────────────────────────────────────────────────────────
    void restorePositionRule_overridesGlobalSetting_viaCompositeWindowId()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PhosphorWindowRule::WindowRuleStore store(dir.filePath(QStringLiteral("windowrules.json")));

        PhosphorWindowRule::WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("no-restore-dolphin");
        rule.enabled = true;
        rule.priority = 100;
        rule.match = PhosphorWindowRule::MatchExpression::makeLeaf(
            PhosphorWindowRule::Field::AppId, PhosphorWindowRule::Operator::Equals, QStringLiteral("org.kde.dolphin"));
        PhosphorWindowRule::RuleAction action;
        action.type = QString(PhosphorWindowRule::ActionType::RestorePosition);
        action.params.insert(QString(PhosphorWindowRule::ActionParam::Value), false);
        rule.actions.append(action);
        QVERIFY(store.addRule(rule));

        m_wta->setWindowRuleStore(&store);
        // Sever the borrow on ANY exit path (incl. an early QVERIFY2 return)
        // before the stack-local store above is destroyed — a dangling borrow
        // would be deref'd during teardown.
        const auto detach = qScopeGuard([this] {
            m_wta->setWindowRuleStore(nullptr);
        });
        m_settings->setSnappingRestoreFloatedWindowsOnLogin(true); // global default ON

        // Dolphin's metadata is registered under the BARE instance id.
        const QString dolphinInstance = QStringLiteral("dolphin-uuid-1");
        m_registry->upsert(dolphinInstance, {QStringLiteral("org.kde.dolphin"), QString(), QString()});

        // The engine consults the predicate with the COMPOSITE windowId (built via
        // the same identity helper the daemon uses) — the rule must still resolve
        // (false) and override the global ON. On the pre-fix code metadata() missed
        // and this returned the global default (true).
        QVERIFY2(!m_wta->shouldRestoreFloatedPosition(
                     PhosphorIdentity::WindowId::buildCompositeId(QStringLiteral("org.kde.dolphin"), dolphinInstance),
                     PhosphorZones::AssignmentEntry::Mode::Snapping),
                 "a matched RestorePosition(false) rule must override the global ON setting");

        // The RestorePosition rule is engine-neutral: it overrides the global for
        // autotile-floated windows too.
        QVERIFY2(!m_wta->shouldRestoreFloatedPosition(
                     PhosphorIdentity::WindowId::buildCompositeId(QStringLiteral("org.kde.dolphin"), dolphinInstance),
                     PhosphorZones::AssignmentEntry::Mode::Autotile),
                 "a matched RestorePosition(false) rule must override the global ON setting (autotile too)");

        // An unmatched window falls back to the global setting (ON).
        const QString konsoleInstance = QStringLiteral("konsole-uuid-1");
        m_registry->upsert(konsoleInstance, {QStringLiteral("org.kde.konsole"), QString(), QString()});
        QVERIFY2(m_wta->shouldRestoreFloatedPosition(
                     PhosphorIdentity::WindowId::buildCompositeId(QStringLiteral("org.kde.konsole"), konsoleInstance),
                     PhosphorZones::AssignmentEntry::Mode::Snapping),
                 "an unmatched window falls back to the global snappingRestoreFloatedWindowsOnLogin = true");
    }

    // ────────────────────────────────────────────────────────────────────
    // Float rule resolves through the composite windowId. shouldFloatByRule is
    // purely rule-driven (no global default): the verdict is the presence of a
    // matched Float slot, resolved through the same bare-instance-id extraction
    // as RestorePosition. An unmatched window — and the no-store case — is false.
    // ────────────────────────────────────────────────────────────────────
    void floatRule_floatsMatchedWindow_viaCompositeWindowId()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PhosphorWindowRule::WindowRuleStore store(dir.filePath(QStringLiteral("windowrules.json")));

        PhosphorWindowRule::WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("float-dolphin");
        rule.enabled = true;
        rule.priority = 100;
        rule.match = PhosphorWindowRule::MatchExpression::makeLeaf(
            PhosphorWindowRule::Field::AppId, PhosphorWindowRule::Operator::Equals, QStringLiteral("org.kde.dolphin"));
        PhosphorWindowRule::RuleAction action;
        action.type = QString(PhosphorWindowRule::ActionType::Float);
        rule.actions.append(action);
        QVERIFY(store.addRule(rule));

        m_wta->setWindowRuleStore(&store);
        const auto detach = qScopeGuard([this] {
            m_wta->setWindowRuleStore(nullptr);
        });

        const QString dolphinInstance = QStringLiteral("dolphin-float-1");
        m_registry->upsert(dolphinInstance, {QStringLiteral("org.kde.dolphin"), QString(), QString()});

        QVERIFY2(m_wta->shouldFloatByRule(
                     PhosphorIdentity::WindowId::buildCompositeId(QStringLiteral("org.kde.dolphin"), dolphinInstance)),
                 "a matched Float rule must open the window floating (resolved via the composite windowId)");

        // An unmatched window is never floated — Float has no global default.
        const QString konsoleInstance = QStringLiteral("konsole-float-1");
        m_registry->upsert(konsoleInstance, {QStringLiteral("org.kde.konsole"), QString(), QString()});
        QVERIFY2(!m_wta->shouldFloatByRule(
                     PhosphorIdentity::WindowId::buildCompositeId(QStringLiteral("org.kde.konsole"), konsoleInstance)),
                 "an unmatched window must not be floated (no global float-on-open default)");
    }

    void floatRule_falsesWithoutStore()
    {
        m_wta->setWindowRuleStore(nullptr);
        QVERIFY2(!m_wta->shouldFloatByRule(PhosphorIdentity::WindowId::buildCompositeId(
                     QStringLiteral("org.kde.dolphin"), QStringLiteral("any-uuid"))),
                 "with no rule store, no window is rule-floated");
    }

    // ────────────────────────────────────────────────────────────────────
    // Extended window properties carried by setWindowMetadata's trailing a{sv}
    // reach the Float resolver, so a rule keyed on a KWin-property / geometry
    // field (not just appId) resolves daemon-side. Exercises the full path:
    // setWindowMetadata QVariantMap unpack → WindowMetadata optionals →
    // buildRuleQueryForWindow → rule evaluation. Also pins the engage-only-when-
    // known contract: ABSENT keys leave the fields disengaged → predicate inert.
    //
    // Each scenario uses a DISTINCT instanceId: shouldFloatByRule resolves through
    // resolveCached, keyed on (windowId, ruleSet revision), so the verdict for a
    // given window is memoised — the production invariant (a window is resolved
    // once at open). Reusing one windowId across differing metadata would read the
    // first cached verdict, not the new metadata.
    // ────────────────────────────────────────────────────────────────────
    void floatRule_matchesExtendedProperty_isModalAndWidth()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PhosphorWindowRule::WindowRuleStore store(dir.filePath(QStringLiteral("windowrules.json")));

        // "Float any modal dialog narrower than 500px" — a bool field AND an int
        // field, both carried only via the extended a{sv}, combined under All.
        PhosphorWindowRule::WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("float-narrow-modal");
        rule.enabled = true;
        rule.priority = 100;
        rule.match = PhosphorWindowRule::MatchExpression::makeAll(
            {PhosphorWindowRule::MatchExpression::makeLeaf(PhosphorWindowRule::Field::IsModal,
                                                           PhosphorWindowRule::Operator::Equals, true),
             PhosphorWindowRule::MatchExpression::makeLeaf(PhosphorWindowRule::Field::Width,
                                                           PhosphorWindowRule::Operator::LessThan, 500)});
        PhosphorWindowRule::RuleAction action;
        action.type = QString(PhosphorWindowRule::ActionType::Float);
        rule.actions.append(action);
        QVERIFY(store.addRule(rule));

        m_wta->setWindowRuleStore(&store);
        const auto detach = qScopeGuard([this] {
            m_wta->setWindowRuleStore(nullptr);
        });

        namespace Key = PhosphorProtocol::Service::WindowMetadataKey;
        const QString appId = QStringLiteral("org.kde.someapp");
        const int normalType = static_cast<int>(PhosphorProtocol::WindowType::Normal);
        // Pushes the extended snapshot for a fresh window and returns its open-time
        // float verdict. Distinct instanceId per call → distinct resolveCached key.
        const auto floatVerdict = [&](const QString& instance, const QVariantMap& extended) {
            m_wta->setWindowMetadata(instance, appId, QString(), QString(), QString(), 0, 0, QString(), normalType,
                                     extended);
            return m_wta->shouldFloatByRule(PhosphorIdentity::WindowId::buildCompositeId(appId, instance));
        };

        // Modal + width 400 → both leaves match → float.
        QVariantMap modalNarrow;
        modalNarrow.insert(Key::IsModal, true);
        modalNarrow.insert(Key::Width, 400);
        QVERIFY2(floatVerdict(QStringLiteral("modal-narrow-1"), modalNarrow),
                 "modal dialog narrower than 500px must float (extended bool + int props matched)");

        // Modal but wide (800) → Width leaf fails → no float.
        QVariantMap modalWide;
        modalWide.insert(Key::IsModal, true);
        modalWide.insert(Key::Width, 800);
        QVERIFY2(!floatVerdict(QStringLiteral("modal-wide-1"), modalWide),
                 "a wide modal must not float (Width >= 500 fails the All)");

        // Narrow but not modal → IsModal leaf fails → no float.
        QVariantMap nonModalNarrow;
        nonModalNarrow.insert(Key::IsModal, false);
        nonModalNarrow.insert(Key::Width, 400);
        QVERIFY2(!floatVerdict(QStringLiteral("nonmodal-narrow-1"), nonModalNarrow),
                 "a non-modal narrow window must not float (IsModal == true fails)");

        // Absent keys → both fields disengaged → predicate inert → no float.
        QVERIFY2(!floatVerdict(QStringLiteral("no-props-1"), QVariantMap()),
                 "with the extended props absent, an IsModal/Width rule stays inert (engage-only-when-known)");
    }

    // ────────────────────────────────────────────────────────────────────
    // The no-rule fallback branches: with no store, or a store that has no
    // metadata for the window, the per-engine *RestoreFloatedWindowsOnLogin
    // setting is the whole policy. Guards the early-outs in
    // shouldRestoreFloatedPosition (null store / registry, metadata miss) —
    // a regression flipping either to a hardcoded false would silently disable
    // restore for every not-yet-registered window at session start.
    // ────────────────────────────────────────────────────────────────────
    void restorePositionRule_fallsBackToGlobal_withoutMatchingMetadata()
    {
        const QString unseen = PhosphorIdentity::WindowId::buildCompositeId(QStringLiteral("org.kde.dolphin"),
                                                                            QStringLiteral("unseen-uuid"));

        // No store wired → per-engine global setting decides, both polarities. The
        // two engine defaults are independent: Mode::Snapping reads the snapping
        // setting, Mode::Autotile reads the autotile setting.
        m_wta->setWindowRuleStore(nullptr);
        m_settings->setSnappingRestoreFloatedWindowsOnLogin(true);
        m_settings->setAutotileRestoreFloatedWindowsOnLogin(false);
        QVERIFY2(m_wta->shouldRestoreFloatedPosition(unseen, PhosphorZones::AssignmentEntry::Mode::Snapping),
                 "no store → snapping global ON restores");
        QVERIFY2(!m_wta->shouldRestoreFloatedPosition(unseen, PhosphorZones::AssignmentEntry::Mode::Autotile),
                 "no store → autotile global OFF suppresses (independent of snapping)");
        m_settings->setSnappingRestoreFloatedWindowsOnLogin(false);
        m_settings->setAutotileRestoreFloatedWindowsOnLogin(true);
        QVERIFY2(!m_wta->shouldRestoreFloatedPosition(unseen, PhosphorZones::AssignmentEntry::Mode::Snapping),
                 "no store → snapping global OFF suppresses");
        QVERIFY2(m_wta->shouldRestoreFloatedPosition(unseen, PhosphorZones::AssignmentEntry::Mode::Autotile),
                 "no store → autotile global ON restores (independent of snapping)");

        // Store wired, but this window was never registered → metadata miss →
        // still the global setting (no rule can match a window with no query).
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PhosphorWindowRule::WindowRuleStore store(dir.filePath(QStringLiteral("windowrules.json")));
        m_wta->setWindowRuleStore(&store);
        const auto detach = qScopeGuard([this] {
            m_wta->setWindowRuleStore(nullptr);
        });
        m_settings->setSnappingRestoreFloatedWindowsOnLogin(true);
        QVERIFY2(m_wta->shouldRestoreFloatedPosition(unseen, PhosphorZones::AssignmentEntry::Mode::Snapping),
                 "store wired but no metadata for this window → global ON");
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetectorReactive* m_zoneDetector = nullptr;
    PhosphorEngine::WindowRegistry* m_registry = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtaReactiveMetadata)
#include "test_wta_reactive_metadata.moc"
