// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Adaptor-facing admission surface tests. The adaptor's
// setVirtualScreenConfig() slot is the choke point D-Bus clients use to
// mutate topology; the contract is that its store backend enforces the
// same cap and validation rules ScreenManager's Config does. A divergence
// used to exist (daemon cap vs. settings cap), and this test exists so
// regressions surface at the lib level rather than inside the PlasmaZones
// daemon's integration paths.

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/DBusScreenAdaptor.h>
#include <PhosphorScreens/InMemoryConfigStore.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/NoOpPanelSource.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSignalSpy>
#include <QString>
#include <QTest>

using Phosphor::Screens::DBusScreenAdaptor;
using Phosphor::Screens::InMemoryConfigStore;
using Phosphor::Screens::NoOpPanelSource;
using Phosphor::Screens::ScreenManager;
using Phosphor::Screens::ScreenManagerConfig;
using Phosphor::Screens::VirtualScreenConfig;
using Phosphor::Screens::VirtualScreenDef;

namespace {

// QDBusAbstractAdaptor is only abstract about the Q_CLASSINFO interface
// name — its slots are plain methods, so for admission-surface tests we can
// construct the base directly without subclassing. The parent QObject just
// has to survive the scope.
class AdaptorHost : public QObject
{
    Q_OBJECT
};

QString physIdForPrimary()
{
    auto* primary = QGuiApplication::primaryScreen();
    return primary ? primary->name() : QString();
}

VirtualScreenDef makeDef(const QString& physId, int index, const QRectF& region)
{
    VirtualScreenDef d;
    d.index = index;
    d.id = PhosphorIdentity::VirtualScreenId::make(physId, index);
    d.physicalScreenId = physId;
    d.displayName = QStringLiteral("VS-%1").arg(index);
    d.region = region;
    return d;
}

/// Build the JSON payload the adaptor's setVirtualScreenConfig slot reads.
/// N evenly-sized vertical strips over the full unit square.
QString buildStripeJson(int count)
{
    QJsonArray screens;
    const double w = 1.0 / count;
    for (int i = 0; i < count; ++i) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("ignored"); // adaptor synthesises this
        obj[QStringLiteral("index")] = i;
        obj[QStringLiteral("displayName")] = QStringLiteral("VS-%1").arg(i);
        obj[QStringLiteral("region")] = QJsonObject{
            {QStringLiteral("x"), i * w},
            {QStringLiteral("y"), 0.0},
            {QStringLiteral("width"), w},
            {QStringLiteral("height"), 1.0},
        };
        screens.append(obj);
    }
    QJsonObject root;
    root[QStringLiteral("screens")] = screens;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace

class TestDBusScreenAdaptor : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testCapEnforcedAtStoreBoundary()
    {
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }

        // Store cap of 2 — matches the daemon's "real" admission cap that
        // the adaptor surface MUST respect. An over-cap write through the
        // adaptor must be rejected by the store (no change to persisted
        // state, no changed() signal).
        InMemoryConfigStore store(/*maxScreensPerPhysical=*/2);

        AdaptorHost host;
        DBusScreenAdaptor adaptor(&host);
        adaptor.setConfigStore(&store);

        QSignalSpy spy(&store, &InMemoryConfigStore::changed);

        adaptor.setVirtualScreenConfig(physId, buildStripeJson(3));

        QCOMPARE(spy.count(), 0);
        QVERIFY(store.get(physId).isEmpty());
    }

    void testAtCapAcceptedThroughAdaptor()
    {
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }

        InMemoryConfigStore store(/*maxScreensPerPhysical=*/2);

        AdaptorHost host;
        DBusScreenAdaptor adaptor(&host);
        adaptor.setConfigStore(&store);

        QSignalSpy spy(&store, &InMemoryConfigStore::changed);

        adaptor.setVirtualScreenConfig(physId, buildStripeJson(2));

        QCOMPARE(spy.count(), 1);
        const VirtualScreenConfig saved = store.get(physId);
        QCOMPARE(saved.screens.size(), 2);
    }

    void testAdaptorPropagatesManagerCapToCallers()
    {
        // End-to-end parity: the lib's ScreenManager holds its own
        // maxVirtualScreensPerPhysical, and the store is the admission
        // backend. Asserting both admit identically prevents the
        // divergence the daemon called out in its cap-wiring comment.
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }

        InMemoryConfigStore store(/*maxScreensPerPhysical=*/2);
        NoOpPanelSource panelSrc;
        ScreenManager mgr(ScreenManagerConfig{
            /*panelSource=*/&panelSrc,
            /*configStore=*/&store,
            /*useGeometrySensors=*/false,
            /*maxVirtualScreensPerPhysical=*/2,
        });
        mgr.start();

        // Direct ScreenManager admission: over-cap must be rejected.
        VirtualScreenConfig tooMany;
        tooMany.physicalScreenId = physId;
        for (int i = 0; i < 3; ++i) {
            tooMany.screens.append(makeDef(physId, i, QRectF(i / 3.0, 0.0, 1.0 / 3.0, 1.0)));
        }
        QVERIFY(!mgr.setVirtualScreenConfig(physId, tooMany));

        // Adaptor admission for the same payload: same outcome — rejected,
        // no state mutation.
        AdaptorHost host;
        DBusScreenAdaptor adaptor(&host);
        adaptor.setScreenManager(&mgr);
        adaptor.setConfigStore(&store);

        QSignalSpy spy(&store, &InMemoryConfigStore::changed);
        adaptor.setVirtualScreenConfig(physId, buildStripeJson(3));
        QCOMPARE(spy.count(), 0);
        QVERIFY(store.get(physId).isEmpty());
    }

    void testRejectionTokensAreStable()
    {
        // D-Bus callers parse these tokens to distinguish failure modes;
        // assert the exact wire strings so accidental renames show up here
        // rather than as silent client-side behaviour changes.
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }
        InMemoryConfigStore store;
        AdaptorHost host;
        DBusScreenAdaptor adaptor(&host);
        adaptor.setConfigStore(&store);

        // Success path: empty string.
        QCOMPARE(adaptor.setVirtualScreenConfig(physId, buildStripeJson(2)), QString());

        // Parse failure → "parse_error".
        QCOMPARE(adaptor.setVirtualScreenConfig(physId, QStringLiteral("not json")), QStringLiteral("parse_error"));

        // Non-object root → "not_object".
        QCOMPARE(adaptor.setVirtualScreenConfig(physId, QStringLiteral("[]")), QStringLiteral("not_object"));

        // Missing 'screens' key → "missing_screens".
        QCOMPARE(adaptor.setVirtualScreenConfig(physId, QStringLiteral("{}")), QStringLiteral("missing_screens"));

        // Single-entry screens[] → "too_few_screens".
        QCOMPARE(adaptor.setVirtualScreenConfig(physId, buildStripeJson(1)), QStringLiteral("too_few_screens"));

        // Missing index field → "missing_index". Hand-build a payload with
        // two entries that omit `index` (buildStripeJson always sets it).
        QJsonArray badScreens;
        for (int i = 0; i < 2; ++i) {
            QJsonObject obj;
            obj[QStringLiteral("region")] = QJsonObject{
                {QStringLiteral("x"), i * 0.5},
                {QStringLiteral("y"), 0.0},
                {QStringLiteral("width"), 0.5},
                {QStringLiteral("height"), 1.0},
            };
            badScreens.append(obj);
        }
        QJsonObject badRoot;
        badRoot[QStringLiteral("screens")] = badScreens;
        const QString badJson = QString::fromUtf8(QJsonDocument(badRoot).toJson(QJsonDocument::Compact));
        QCOMPARE(adaptor.setVirtualScreenConfig(physId, badJson), QStringLiteral("missing_index"));

        // Empty physicalScreenId → "empty_physical_id".
        QCOMPARE(adaptor.setVirtualScreenConfig(QString(), buildStripeJson(2)), QStringLiteral("empty_physical_id"));

        // No config store wired → "no_config_store".
        AdaptorHost host2;
        DBusScreenAdaptor adaptor2(&host2);
        QCOMPARE(adaptor2.setVirtualScreenConfig(physId, buildStripeJson(2)), QStringLiteral("no_config_store"));
    }
};

QTEST_MAIN(TestDBusScreenAdaptor)
#include "test_dbusscreenadaptor.moc"
