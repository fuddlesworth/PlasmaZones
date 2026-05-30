// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden parity harness: for each ported algorithm, the new Luau engine
// (LuauTileAlgorithm reading data/algorithms/NAME.luau) must produce byte-
// identical zones to the prior JS engine (ScriptedAlgorithm reading NAME.js)
// across a matrix of window counts, gaps, areas, and min-size variants.
//
// This is the gate for Phase 3: an algorithm is only "ported" once it is green
// here.

#include <QtTest>

#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmWatchdog.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorTiles;

class TestLuauParity : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<ScriptedAlgorithmWatchdog> m_jsWatchdog;
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_luaWatchdog;

    static QString jsPath(const QString& name)
    {
        return QStringLiteral(PZ_SOURCE_DIR "/data/algorithms/") + name + QStringLiteral(".js");
    }
    static QString luaPath(const QString& name)
    {
        return QStringLiteral(PZ_SOURCE_DIR "/data/algorithms/") + name + QStringLiteral(".luau");
    }

    void compareAlgorithm(const QString& name);

private Q_SLOTS:
    void initTestCase()
    {
        m_jsWatchdog = std::make_shared<ScriptedAlgorithmWatchdog>();
        m_luaWatchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
    }

    void columns()
    {
        compareAlgorithm(QStringLiteral("columns"));
    }
    void rows()
    {
        compareAlgorithm(QStringLiteral("rows"));
    }
    void monocle()
    {
        compareAlgorithm(QStringLiteral("monocle"));
    }
};

void TestLuauParity::compareAlgorithm(const QString& name)
{
    ScriptedAlgorithm js(jsPath(name), m_jsWatchdog);
    QVERIFY2(js.isValid(), qPrintable(QStringLiteral("JS %1 failed to load").arg(name)));
    LuauTileAlgorithm lua(luaPath(name), m_luaWatchdog);
    QVERIFY2(lua.isValid(), qPrintable(QStringLiteral("Luau %1 failed to load").arg(name)));

    const QList<QRect> areas = {QRect(0, 0, 1920, 1080), QRect(0, 0, 1366, 768), QRect(0, 0, 800, 1280)};
    const QList<int> gaps = {0, 8, 20};
    const QList<int> counts = {1, 2, 3, 4, 5, 6};

    // Min-size variants: none, and a per-window set (truncated to count by the algos).
    const QVector<QSize> someMins = {QSize(200, 150), QSize(250, 120), QSize(180, 200),
                                     QSize(300, 100), QSize(220, 160), QSize(190, 140)};
    const QList<QVector<QSize>> minVariants = {QVector<QSize>{}, someMins};

    for (const QRect& area : areas) {
        for (int gap : gaps) {
            for (int count : counts) {
                for (const QVector<QSize>& mins : minVariants) {
                    TilingState state(QStringLiteral("screen-1"));
                    TilingParams p;
                    p.windowCount = count;
                    p.screenGeometry = area;
                    p.state = &state;
                    p.innerGap = gap;
                    p.outerGaps = EdgeGaps::uniform(0);
                    p.minSizes = mins;

                    const QVector<QRect> jsZones = js.calculateZones(p);
                    const QVector<QRect> luaZones = lua.calculateZones(p);

                    if (jsZones != luaZones) {
                        qWarning().noquote()
                            << "PARITY MISMATCH" << name << "area=" << area << "gap=" << gap << "count=" << count
                            << "mins=" << mins.size() << "\n  js  =" << jsZones << "\n  luau=" << luaZones;
                    }
                    QCOMPARE(luaZones, jsZones);
                }
            }
        }
    }
}

QTEST_MAIN(TestLuauParity)
#include "test_luau_parity.moc"
