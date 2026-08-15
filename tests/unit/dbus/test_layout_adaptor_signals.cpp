// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_adaptor_signals.cpp
 * @brief LayoutAdaptor signal emission contract tests.
 *
 * Pins the rule: property mutations (setLayoutHidden, setLayoutAutoAssign,
 * setLayoutAspectRatioClass) emit the compact
 * `layoutPropertyChanged(layoutId, property, value)` and NEITHER
 * `layoutChanged` NOR `layoutListChanged`. The whole-layout and list signals
 * are reserved for genuine add/delete/reload operations (onLayoutsChanged,
 * notifyLayoutListChanged).
 *
 * Read the tests, not an older description of them: the assertions below
 * require layoutChanged.count() == 0. An earlier revision of this comment
 * described the Phase-1.2 state, where property mutations still emitted
 * layoutChanged and only the list signal was dropped. Anyone "restoring" that
 * emission on the strength of the comment would fail every test in this file.
 *
 * Also covers the active-layout-per-screen wire (discussion #919): the
 * snapshot readback, the changed-screens broadcast, and the empty-id contract.
 */

#include <QTest>
#include <QDBusVariant>
#include <QScopeGuard>
#include <QSignalSpy>

#include "dbus/layoutadaptor/layoutadaptor.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneJsonKeys.h>
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorLayoutApi/LayoutId.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
// Minimal stub algorithm producing two side-by-side zones. Lets the
// autotile getLayout() path run end-to-end without depending on the Luau
// engine (whose geometry is independently covered by test_luau_parity); this
// test pins the *serialization contract* getLayout() owes the editor.
class TwoColumnStubAlgorithm : public PhosphorTiles::TilingAlgorithm
{
public:
    QString name() const override
    {
        return QStringLiteral("Two Column Stub");
    }
    QString description() const override
    {
        return QStringLiteral("Test stub");
    }
    QVector<QRect> calculateZones(const PhosphorTiles::TilingParams& params) const override
    {
        const QRect a = params.screenGeometry;
        const int halfW = a.width() / 2;
        return {QRect(a.x(), a.y(), halfW, a.height()), QRect(a.x() + halfW, a.y(), a.width() - halfW, a.height())};
    }
};
} // namespace

class TestLayoutAdaptorSignals : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), m_parent);
        auto* layout = new PhosphorZones::Layout(QStringLiteral("SignalTestLayout"));
        for (int i = 0; i < 2; ++i) {
            auto* zone = new PhosphorZones::Zone(QRectF(0.5 * i, 0.0, 0.5, 1.0));
            zone->setZoneNumber(i + 1);
            layout->addZone(zone);
        }
        m_layoutManager->addLayout(layout);
        m_layoutId = layout->id().toString();
        m_adaptor = new LayoutAdaptor(m_layoutManager, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_layoutManager = nullptr;
        m_adaptor = nullptr;
        m_guard.reset();
    }

    // ─────────────────────────────────────────────────────────────────
    // Phase 4: property mutations emit the compact layoutPropertyChanged
    // signal, NOT the heavyweight layoutChanged(json) or layoutListChanged.
    // Payload on the wire is (layoutId, property, value) instead of a
    // 5–20 KB full-layout JSON blob.
    // ─────────────────────────────────────────────────────────────────
    void testSetLayoutHidden_emitsCompactPropertySignalOnly()
    {
        QSignalSpy changed(m_adaptor, &LayoutAdaptor::layoutChanged);
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);
        QSignalSpy propertyChanged(m_adaptor, &LayoutAdaptor::layoutPropertyChanged);

        m_adaptor->setLayoutHidden(m_layoutId, true);

        QCOMPARE(changed.count(), 0);
        QCOMPARE(listChanged.count(), 0);
        QCOMPARE(propertyChanged.count(), 1);

        const QList<QVariant> args = propertyChanged.takeFirst();
        QCOMPARE(args.at(0).toString(), m_layoutId);
        QCOMPARE(args.at(1).toString(), QStringLiteral("hidden"));
        // QDBusVariant unwraps via .variant(); in-process connections keep
        // the variant identity, so args.at(2) is a QVariant holding a
        // QDBusVariant holding a bool.
        const QDBusVariant wrapped = args.at(2).value<QDBusVariant>();
        QCOMPARE(wrapped.variant().toBool(), true);
    }

    void testSetLayoutAutoAssign_emitsCompactPropertySignalOnly()
    {
        QSignalSpy changed(m_adaptor, &LayoutAdaptor::layoutChanged);
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);
        QSignalSpy propertyChanged(m_adaptor, &LayoutAdaptor::layoutPropertyChanged);

        m_adaptor->setLayoutAutoAssign(m_layoutId, true);

        QCOMPARE(changed.count(), 0);
        QCOMPARE(listChanged.count(), 0);
        QCOMPARE(propertyChanged.count(), 1);
        const QList<QVariant> args = propertyChanged.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("autoAssign"));
        QCOMPARE(args.at(2).value<QDBusVariant>().variant().toBool(), true);
    }

    void testSetLayoutAspectRatioClass_emitsCompactPropertySignalOnly()
    {
        QSignalSpy changed(m_adaptor, &LayoutAdaptor::layoutChanged);
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);
        QSignalSpy propertyChanged(m_adaptor, &LayoutAdaptor::layoutPropertyChanged);

        m_adaptor->setLayoutAspectRatioClass(m_layoutId, 2);

        QCOMPARE(changed.count(), 0);
        QCOMPARE(listChanged.count(), 0);
        QCOMPARE(propertyChanged.count(), 1);
        const QList<QVariant> args = propertyChanged.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("aspectRatioClass"));
        QCOMPARE(args.at(2).value<QDBusVariant>().variant().toInt(), 2);
    }

    // ─────────────────────────────────────────────────────────────────
    // notifyLayoutListChanged: the one path that SHOULD emit
    // layoutListChanged (daemon calls it after Apply-time reloads).
    // This is the inverse of the rule above — guards against
    // over-zealous removal of the emission.
    // ─────────────────────────────────────────────────────────────────
    void testNotifyLayoutListChanged_emitsListChanged()
    {
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);

        m_adaptor->notifyLayoutListChanged();

        QCOMPARE(listChanged.count(), 1);
    }

    // ─────────────────────────────────────────────────────────────────
    // Phase 1.3: getLayout must never serve stale JSON after a property
    // mutation. Before the fix, m_cachedLayoutJson was populated on the
    // first getLayout() call but never invalidated by setLayoutHidden,
    // setLayoutAutoAssign, or setLayoutAspectRatioClass — so a second
    // getLayout() call would return the pre-mutation JSON.
    // ─────────────────────────────────────────────────────────────────
    void testGetLayout_cacheInvalidated_afterSetLayoutHidden()
    {
        // Prime the cache.
        const QString beforeJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!beforeJson.isEmpty());
        // PhosphorZones::Layout::toJson() only serializes the hiddenFromSelector key when
        // the flag is true, so beforeJson (default false) must not contain it.
        QVERIFY(!beforeJson.contains(QLatin1String("hiddenFromSelector")));

        m_adaptor->setLayoutHidden(m_layoutId, true);

        // The next read must reflect the new value — no stale cache.
        const QString afterJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!afterJson.isEmpty());
        QVERIFY(afterJson.contains(QLatin1String("hiddenFromSelector")));
        QVERIFY(afterJson != beforeJson);
    }

    void testGetLayout_cacheInvalidated_afterSetLayoutAutoAssign()
    {
        const QString beforeJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!beforeJson.isEmpty());
        QVERIFY(!beforeJson.contains(QLatin1String("autoAssign")));

        m_adaptor->setLayoutAutoAssign(m_layoutId, true);

        const QString afterJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(afterJson.contains(QLatin1String("autoAssign")));
        QVERIFY(afterJson != beforeJson);
    }

    void testGetLayout_cacheInvalidated_afterSetLayoutAspectRatioClass()
    {
        const QString beforeJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!beforeJson.isEmpty());

        m_adaptor->setLayoutAspectRatioClass(m_layoutId, 2);

        const QString afterJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(afterJson != beforeJson);
    }

    // ─────────────────────────────────────────────────────────────────
    // Value-equality guard: calling setLayoutHidden/AutoAssign/AspectRatioClass
    // with the currently-stored value must short-circuit — no signal
    // emission, no cache invalidation. Mirrors SettingsAdaptor::setSetting's
    // Phase 1.1 guard so settled checkboxes don't spam subscribers with
    // no-op reloads.
    // ─────────────────────────────────────────────────────────────────
    void testSetLayoutHidden_sameValue_noSignal()
    {
        // Flip to a known state first so the next (same-value) call can
        // exercise the guard. Both the flip and the guard path must
        // produce exactly one propertyChanged signal between them.
        m_adaptor->setLayoutHidden(m_layoutId, true);

        QSignalSpy propertyChanged(m_adaptor, &LayoutAdaptor::layoutPropertyChanged);
        m_adaptor->setLayoutHidden(m_layoutId, true);
        QCOMPARE(propertyChanged.count(), 0);
    }

    void testSetLayoutAutoAssign_sameValue_noSignal()
    {
        m_adaptor->setLayoutAutoAssign(m_layoutId, true);

        QSignalSpy propertyChanged(m_adaptor, &LayoutAdaptor::layoutPropertyChanged);
        m_adaptor->setLayoutAutoAssign(m_layoutId, true);
        QCOMPARE(propertyChanged.count(), 0);
    }

    void testSetLayoutAspectRatioClass_sameValue_noSignal()
    {
        m_adaptor->setLayoutAspectRatioClass(m_layoutId, 2);

        QSignalSpy propertyChanged(m_adaptor, &LayoutAdaptor::layoutPropertyChanged);
        m_adaptor->setLayoutAspectRatioClass(m_layoutId, 2);
        QCOMPARE(propertyChanged.count(), 0);
    }

    // Regression guard: getLayout() is the editor's load endpoint
    // (DBusLayoutService::loadLayout → getLayout). Its sole consumer parses
    // the canonical Layout schema — top-level `name`, and each zone's geometry
    // nested under `relativeGeometry`. The autotile branch must emit that
    // schema, NOT the flat preview schema (zones[].x/width at top level) used
    // by getLayoutPreview*/OSD. Emitting the flat shape silently parsed every
    // editor zone to (0,0,0,0) — a blank canvas in preview mode.
    void testGetLayout_autotile_emitsLayoutSchemaNotPreviewSchema()
    {
        namespace K = ::PhosphorZones::ZoneJsonKeys;

        PhosphorTiles::AlgorithmRegistry registry;
        registry.registerAlgorithm(QStringLiteral("twocol"), new TwoColumnStubAlgorithm);
        m_adaptor->setAlgorithmRegistry(&registry);
        // Detach on EVERY exit path, not just the success one. The adaptor is owned
        // by m_parent and lives until cleanup(), while this registry dies at the end
        // of the method, and layoutadaptor.h documents the pointer as "Borrowed;
        // outlives adaptor". Any QVERIFY2 below returns early, so a bare clear at the
        // bottom would leave the adaptor holding a destroyed registry through
        // teardown — turning one failing assertion into a crash that loses the rest
        // of the run.
        const auto detachRegistry = qScopeGuard([this] {
            m_adaptor->setAlgorithmRegistry(nullptr);
        });

        const QString autotileId = PhosphorLayout::LayoutId::makeAutotileId(QStringLiteral("twocol"));
        const QString jsonStr = m_adaptor->getLayout(autotileId);
        QVERIFY2(!jsonStr.isEmpty(), "getLayout returned empty JSON for a registered autotile algorithm");

        const QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QCOMPARE(obj.value(K::Id).toString(), autotileId);
        // Editor reads the layout title from `name`, not the preview's `displayName`.
        QCOMPARE(obj.value(K::Name).toString(), QStringLiteral("Two Column Stub"));

        const QJsonArray zones = obj.value(K::Zones).toArray();
        QCOMPARE(zones.size(), 2);

        const QJsonObject zone0 = zones.at(0).toObject();
        // Must be the nested Layout schema...
        QVERIFY2(zone0.contains(K::RelativeGeometry), "zone missing relativeGeometry — editor would parse (0,0,0,0)");
        // ...and NOT the flat preview schema the bug emitted.
        QVERIFY2(!zone0.contains(K::Width), "zone leaked flat preview-schema width key");
        QVERIFY2(!zone0.contains(K::X), "zone leaked flat preview-schema x key");

        const QJsonObject relGeo = zone0.value(K::RelativeGeometry).toObject();
        QVERIFY(relGeo.value(K::Width).toDouble() > 0.0);
        QVERIFY(relGeo.value(K::Height).toDouble() > 0.0);
        QCOMPARE(relGeo.value(K::Width).toDouble(), 0.5);
        QCOMPARE(zone0.value(K::ZoneNumber).toInt(), 1);
        QVERIFY2(!zone0.value(K::Id).toString().isEmpty(), "zone needs a stable id (editor keys zones by id)");
    }

    // ─────────────────────────────────────────────────────────────────
    // Active-layout-per-screen wire (discussion #919). The daemon owns the
    // assignment cascade and pushes its recomputed snapshot here; the adaptor
    // serves it as the readback and broadcasts only the screens that moved.
    // The KWin effect consumes both to match the ActiveLayout rule field, which
    // it cannot resolve on its own.
    // ─────────────────────────────────────────────────────────────────
    void testPublishActiveAssignments_broadcastsOnlyChangedScreens()
    {
        QSignalSpy spy(m_adaptor, &LayoutAdaptor::activeLayoutForScreenChanged);

        const QHash<QString, QString> snapshot{{QStringLiteral("DP-1"), m_layoutId},
                                               {QStringLiteral("DP-2"), QStringLiteral("autotile:bsp")}};
        m_adaptor->publishActiveAssignments(snapshot, {QStringLiteral("DP-1")});

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(args.at(1).toString(), m_layoutId);

        // The readback carries the WHOLE snapshot, not just the broadcast screen:
        // a subscriber seeding at bringup has to see every screen, because the
        // signal stream from then on only carries deltas.
        const QVariantMap readback = m_adaptor->getActiveLayoutsForScreens();
        QCOMPARE(readback.size(), 2);
        QCOMPARE(readback.value(QStringLiteral("DP-1")).toString(), m_layoutId);
        QCOMPARE(readback.value(QStringLiteral("DP-2")).toString(), QStringLiteral("autotile:bsp"));
    }

    void testPublishActiveAssignments_emptyIdIsBroadcastButNotReadBack()
    {
        m_adaptor->publishActiveAssignments({{QStringLiteral("DP-1"), m_layoutId}}, {QStringLiteral("DP-1")});

        QSignalSpy spy(m_adaptor, &LayoutAdaptor::activeLayoutForScreenChanged);
        QVERIFY(spy.isValid());
        // DP-1 unplugged: it drops out of the snapshot entirely.
        m_adaptor->publishActiveAssignments({}, {QStringLiteral("DP-1")});

        // The screen still has to be announced, with an empty id, or a subscriber
        // keeps matching windows against a layout that is no longer on it.
        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("DP-1"));
        QVERIFY2(args.at(1).toString().isEmpty(), "a dropped screen must broadcast an empty layout id");

        QVERIFY(m_adaptor->getActiveLayoutsForScreens().isEmpty());
    }

    void testGetActiveLayoutsForScreens_omitsPresentButEmptyEntries()
    {
        // The readback's empty-value filter guards a screen that is PRESENT in
        // the snapshot carrying an EMPTY id, which is a state the producer really
        // makes: diffActiveAssignments inserts assignmentIdForScreen for every
        // effective screen, and that resolves empty for a connected screen with no
        // assignment and the global default suppressed.
        //
        // Publishing an all-empty snapshot would NOT exercise the filter — the map
        // would have no entries to iterate either way, so the assertion would pass
        // with the guard deleted. Publish a mixed one so it actually discriminates.
        m_adaptor->publishActiveAssignments({{QStringLiteral("DP-1"), QString()}, {QStringLiteral("DP-2"), m_layoutId}},
                                            {QStringLiteral("DP-1"), QStringLiteral("DP-2")});

        const QVariantMap readback = m_adaptor->getActiveLayoutsForScreens();
        QCOMPARE(readback.size(), 1);
        QVERIFY2(!readback.contains(QStringLiteral("DP-1")),
                 "a present-but-empty entry must be omitted, not carried as an empty string");
        QCOMPARE(readback.value(QStringLiteral("DP-2")).toString(), m_layoutId);
    }

    void testPublishActiveAssignments_emptyChangedSetIsSilent()
    {
        m_adaptor->publishActiveAssignments({{QStringLiteral("DP-1"), m_layoutId}}, {QStringLiteral("DP-1")});

        QSignalSpy spy(m_adaptor, &LayoutAdaptor::activeLayoutForScreenChanged);
        QVERIFY(spy.isValid());
        // A recompute that finds nothing moved republishes the same snapshot with
        // an empty changed set. The readback must still be refreshed, but the bus
        // must stay quiet — the effect pairs every broadcast with a full rule-cache
        // invalidation and a decoration sweep, so a spurious one is not free.
        m_adaptor->publishActiveAssignments({{QStringLiteral("DP-1"), m_layoutId}}, {});

        QCOMPARE(spy.count(), 0);
        QCOMPARE(m_adaptor->getActiveLayoutsForScreens().size(), 1);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    LayoutAdaptor* m_adaptor = nullptr;
    QString m_layoutId;
};

QTEST_MAIN(TestLayoutAdaptorSignals)
#include "test_layout_adaptor_signals.moc"
