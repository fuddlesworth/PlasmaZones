// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// StripKeys: which scrolling strip a screen id names, and the rect its edges
// are measured against. These replace the old collapse of every screen id onto
// its whole output, under which two virtual screens on one monitor shared one
// spring and one tab band, and a strip's edges were the monitor's.

#include "tilinghandler/stripkeys.h"

#include <QList>
#include <QtTest>

using namespace PlasmaZones;

namespace {

/// The two fields StripKeys::screenRect reads, standing in for the effect's
/// EffectVirtualScreenDef.
struct Def
{
    QString id;
    QRect geometry;
};

const QRect kOutput(0, 0, 1920, 1080);

QList<Def> splitDefs()
{
    return {{QStringLiteral("S/vs:0"), QRect(0, 0, 960, 1080)}, {QStringLiteral("S/vs:1"), QRect(960, 0, 960, 1080)}};
}

} // namespace

class TestStripKeys : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void virtualScreenIsAStripOfItsOwn()
    {
        const QString right = StripKeys::stripKey(QStringLiteral("Virtual-0/vs:1"), QStringLiteral("Virtual-0"));
        QCOMPARE(right, QStringLiteral("Virtual-0/vs:1"));
        QVERIFY(right != StripKeys::stripKey(QStringLiteral("Virtual-0/vs:0"), QStringLiteral("Virtual-0")));
    }

    void twoSpellingsOfOneMonitorShareAStrip()
    {
        QCOMPARE(StripKeys::stripKey(QStringLiteral("DP-1"), QStringLiteral("Dell:U27:1")),
                 StripKeys::stripKey(QStringLiteral("Dell:U27:1"), QStringLiteral("Dell:U27:1")));
    }

    void unresolvedPhysicalKeepsItsId()
    {
        QCOMPARE(StripKeys::stripKey(QStringLiteral("DP-9"), QString()), QStringLiteral("DP-9"));
    }

    void virtualScreenEdgesAreItsRegion()
    {
        QCOMPARE(StripKeys::screenRect(QStringLiteral("S/vs:1"), kOutput, splitDefs()), QRect(960, 0, 960, 1080));
        QCOMPARE(StripKeys::screenRect(QStringLiteral("S/vs:0"), kOutput, splitDefs()), QRect(0, 0, 960, 1080));
    }

    void physicalScreenEdgesAreTheOutput()
    {
        QCOMPARE(StripKeys::screenRect(QStringLiteral("S"), kOutput, splitDefs()), kOutput);
    }

    void undefinedVirtualScreenFallsBackToTheOutput()
    {
        QCOMPARE(StripKeys::screenRect(QStringLiteral("S/vs:7"), kOutput, splitDefs()), kOutput);
        QCOMPARE(StripKeys::screenRect(QStringLiteral("S/vs:1"), kOutput, QList<Def>()), kOutput);
    }

    /// The question the cascade, the edge-less arm and the park cull ask: is a
    /// rect on this strip? A column parked inside the sibling is not, though
    /// it is on the monitor, which is what the old whole-output rect answered.
    void aParkInTheSiblingIsNotOnTheStrip()
    {
        const QRect parkedInLeft(200, 100, 600, 800);
        QVERIFY(!StripKeys::screenRect(QStringLiteral("S/vs:1"), kOutput, splitDefs()).intersects(parkedInLeft));
        QVERIFY(kOutput.intersects(parkedInLeft));
    }
};

QTEST_MAIN(TestStripKeys)
#include "test_strip_keys.moc"
