// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerProfile.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace PhosphorPointerShaders;

/// The pointer chain is persisted as one nested JSON object under
/// Pointer/Chain and shipped to the compositor as a JSON string, so the
/// round trip is a wire contract rather than an implementation detail.
class TestPointerProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testEmptyProfileRoundTrips();
    void testLayerOrderIsPreserved();
    void testParametersRoundTrip();
    void testEnabledDefaultsTrueWhenAbsent();
    void testEntriesWithoutEffectIdAreDropped();
    void testMalformedInputYieldsEmptyProfile();
    void testEqualityComparesEveryField();
};

void TestPointerProfile::testEmptyProfileRoundTrips()
{
    const PointerProfile empty;
    QVERIFY(empty.isEmpty());

    const QJsonObject json = empty.toJson();
    // The key must exist even when empty: the compositor distinguishes "no
    // chain configured" from "malformed payload" by parsing, not by probing.
    QVERIFY(json.contains(QLatin1String("layers")));
    QCOMPARE(json.value(QLatin1String("layers")).toArray().size(), 0);

    const PointerProfile back = PointerProfile::fromJson(json);
    QVERIFY(back.isEmpty());
    QCOMPARE(back, empty);
}

void TestPointerProfile::testLayerOrderIsPreserved()
{
    // Chain order is the composite order, so a reordering bug would silently
    // paint the halo over the trail instead of under it.
    PointerProfile profile;
    profile.layers.append(PointerLayer{QStringLiteral("halo"), {}, true});
    profile.layers.append(PointerLayer{QStringLiteral("phosphor-trail"), {}, true});
    profile.layers.append(PointerLayer{QStringLiteral("sparks"), {}, true});

    const PointerProfile back = PointerProfile::fromJson(profile.toJson());
    QCOMPARE(back.layers.size(), 3);
    QCOMPARE(back.layers.at(0).effectId, QStringLiteral("halo"));
    QCOMPARE(back.layers.at(1).effectId, QStringLiteral("phosphor-trail"));
    QCOMPARE(back.layers.at(2).effectId, QStringLiteral("sparks"));
    QCOMPARE(back, profile);
}

void TestPointerProfile::testParametersRoundTrip()
{
    PointerProfile profile;
    QVariantMap params;
    params.insert(QStringLiteral("width"), 12.5);
    params.insert(QStringLiteral("colorA"), QStringLiteral("#ff22d3ee"));
    params.insert(QStringLiteral("count"), 7);
    profile.layers.append(PointerLayer{QStringLiteral("phosphor-trail"), params, false});

    const PointerProfile back = PointerProfile::fromJson(profile.toJson());
    QCOMPARE(back.layers.size(), 1);
    const PointerLayer& layer = back.layers.at(0);
    QCOMPARE(layer.effectId, QStringLiteral("phosphor-trail"));
    QCOMPARE(layer.enabled, false);
    QCOMPARE(layer.parameters.value(QStringLiteral("width")).toDouble(), 12.5);
    QCOMPARE(layer.parameters.value(QStringLiteral("colorA")).toString(), QStringLiteral("#ff22d3ee"));
    QCOMPARE(layer.parameters.value(QStringLiteral("count")).toInt(), 7);
}

void TestPointerProfile::testEnabledDefaultsTrueWhenAbsent()
{
    // A hand-edited config or a third-party writer may omit the flag. A
    // layer someone deliberately added should run, so absence means enabled.
    QJsonObject layer;
    layer.insert(QLatin1String("effectId"), QStringLiteral("comet"));
    QJsonArray layers;
    layers.append(layer);
    QJsonObject root;
    root.insert(QLatin1String("layers"), layers);

    const PointerProfile profile = PointerProfile::fromJson(root);
    QCOMPARE(profile.layers.size(), 1);
    QVERIFY(profile.layers.at(0).enabled);
}

void TestPointerProfile::testEntriesWithoutEffectIdAreDropped()
{
    // An empty id resolves to no pack, so keeping the entry would show a
    // blank row in the chain editor that the compositor silently skips.
    QJsonArray layers;
    QJsonObject named;
    named.insert(QLatin1String("effectId"), QStringLiteral("halo"));
    layers.append(named);
    layers.append(QJsonObject{});
    QJsonObject blank;
    blank.insert(QLatin1String("effectId"), QString());
    layers.append(blank);
    QJsonObject root;
    root.insert(QLatin1String("layers"), layers);

    const PointerProfile profile = PointerProfile::fromJson(root);
    QCOMPARE(profile.layers.size(), 1);
    QCOMPARE(profile.layers.at(0).effectId, QStringLiteral("halo"));
}

void TestPointerProfile::testMalformedInputYieldsEmptyProfile()
{
    // Fail closed: a payload that is not shaped like a chain must decorate
    // nothing rather than throwing or half-populating.
    QCOMPARE(PointerProfile::fromJson(QJsonObject{}).layers.size(), 0);

    QJsonObject wrongType;
    wrongType.insert(QLatin1String("layers"), QStringLiteral("not-an-array"));
    QCOMPARE(PointerProfile::fromJson(wrongType).layers.size(), 0);

    QJsonObject scalarEntries;
    QJsonArray entries;
    entries.append(42);
    entries.append(QStringLiteral("halo"));
    scalarEntries.insert(QLatin1String("layers"), entries);
    QCOMPARE(PointerProfile::fromJson(scalarEntries).layers.size(), 0);
}

void TestPointerProfile::testEqualityComparesEveryField()
{
    // Settings only writes and only signals when the value actually changed,
    // so an equality operator that ignored a field would drop real edits.
    PointerProfile a;
    a.layers.append(PointerLayer{QStringLiteral("halo"), {}, true});

    PointerProfile differentId = a;
    differentId.layers[0].effectId = QStringLiteral("comet");
    QVERIFY(differentId != a);

    PointerProfile differentEnabled = a;
    differentEnabled.layers[0].enabled = false;
    QVERIFY(differentEnabled != a);

    PointerProfile differentParams = a;
    differentParams.layers[0].parameters.insert(QStringLiteral("radius"), 10);
    QVERIFY(differentParams != a);

    PointerProfile same = a;
    QVERIFY(same == a);
}

QTEST_MAIN(TestPointerProfile)
#include "test_pointerprofile.moc"
