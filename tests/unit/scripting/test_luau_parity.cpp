// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden-snapshot regression harness for the bundled Luau tiling algorithms.
//
// During the migration this compared the Luau engine against the prior JS
// engine and proved byte-identical output. With the JS engine removed, it now
// compares the Luau output against a committed golden fixture
// (data/golden_zones.txt) captured from that proven-correct output, across the
// same matrix of counts/gaps/areas/ratios/master-counts/min-sizes.
//
// Oracle caveat: every algorithm's fixture except `bsp` was captured while the
// JS engine still existed, so those cases are anchored to an independent
// oracle. `bsp` parity cases were (re)generated from the Luau output itself, so
// for bsp this harness is a regression guard, not an independent-correctness
// check — bsp's independent correctness is covered by test_tiling_algo_bsp.cpp
// (hand-worked geometry assertions). Keep that test authoritative for bsp.
//
// Regenerate the fixture after an intentional output change:
//   P_GENERATE_GOLDEN=1 ./build/bin/test_luau_parity

#include <QtTest>

#include <QFile>
#include <QMap>
#include <QSet>
#include <QTextStream>

#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorTiles;

namespace {
QString goldenPath()
{
    return QStringLiteral(P_SOURCE_DIR "/tests/unit/scripting/data/golden_zones.txt");
}

QString zonesToStr(const QVector<QRect>& zones)
{
    QStringList parts;
    parts.reserve(zones.size());
    for (const QRect& z : zones) {
        parts << QStringLiteral("%1,%2,%3,%4").arg(z.x()).arg(z.y()).arg(z.width()).arg(z.height());
    }
    return parts.join(QLatin1Char(';'));
}
} // namespace

class TestLuauParity : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;
    QMap<QString, QString> m_golden;
    QSet<QString> m_seen; ///< keys actually produced this run (orphan-golden guard)
    bool m_generate = false;

    static QString luaPath(const QString& name)
    {
        return QStringLiteral(P_SOURCE_DIR "/data/algorithms/") + name + QStringLiteral(".luau");
    }

    // Record (generate mode) or compare (normal mode) a case's zones.
    void record(const QString& key, const QVector<QRect>& zones)
    {
        const QString val = zonesToStr(zones);
        m_seen.insert(key);
        if (m_generate) {
            m_golden.insert(key, val);
            return;
        }
        QVERIFY2(m_golden.contains(key), qPrintable(QStringLiteral("missing golden entry: %1").arg(key)));
        if (m_golden.value(key) != val) {
            qWarning().noquote() << "GOLDEN MISMATCH" << key << "\n  golden=" << m_golden.value(key)
                                 << "\n  luau  =" << val;
        }
        QCOMPARE(val, m_golden.value(key));
    }

    void runMatrix(const QString& name);

private Q_SLOTS:
    void initTestCase()
    {
        m_watchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
        m_generate = qEnvironmentVariableIsSet("P_GENERATE_GOLDEN");
        if (!m_generate) {
            QFile f(goldenPath());
            QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
                     "golden fixture missing — run with P_GENERATE_GOLDEN=1 to create it");
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString line = in.readLine();
                const int tab = line.indexOf(QLatin1Char('\t'));
                if (tab > 0) {
                    m_golden.insert(line.left(tab), line.mid(tab + 1));
                }
            }
        }
    }

    void cleanupTestCase()
    {
        if (!m_generate) {
            // Every golden row must have been produced this run; a leftover
            // (orphan) key means the matrix shrank or an algorithm was removed
            // without regenerating the fixture — which the per-key compare alone
            // would not catch.
            for (auto it = m_golden.constBegin(); it != m_golden.constEnd(); ++it) {
                if (!m_seen.contains(it.key())) {
                    qWarning().noquote() << "ORPHAN golden key (never produced):" << it.key();
                }
            }
            QCOMPARE(m_seen.size(), m_golden.size());
            return;
        }
        QFile f(goldenPath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        for (auto it = m_golden.constBegin(); it != m_golden.constEnd(); ++it) {
            out << it.key() << '\t' << it.value() << '\n';
        }
    }

    void columns()
    {
        runMatrix(QStringLiteral("columns"));
    }
    void rows()
    {
        runMatrix(QStringLiteral("rows"));
    }
    void monocle()
    {
        runMatrix(QStringLiteral("monocle"));
    }
    void deck()
    {
        runMatrix(QStringLiteral("deck"));
    }
    void horizontalDeck()
    {
        runMatrix(QStringLiteral("horizontal-deck"));
    }
    void masterStack()
    {
        runMatrix(QStringLiteral("master-stack"));
    }
    void wide()
    {
        runMatrix(QStringLiteral("wide"));
    }
    void paper()
    {
        runMatrix(QStringLiteral("paper"));
    }
    void stair()
    {
        runMatrix(QStringLiteral("stair"));
    }
    void zen()
    {
        runMatrix(QStringLiteral("zen"));
    }
    void focusSidebar()
    {
        runMatrix(QStringLiteral("focus-sidebar"));
    }
    void quadrantPriority()
    {
        runMatrix(QStringLiteral("quadrant-priority"));
    }
    void cornerMaster()
    {
        runMatrix(QStringLiteral("corner-master"));
    }
    void dwindle()
    {
        runMatrix(QStringLiteral("dwindle"));
    }
    void spread()
    {
        runMatrix(QStringLiteral("spread"));
    }
    void grid()
    {
        runMatrix(QStringLiteral("grid"));
    }
    void threeColumn()
    {
        runMatrix(QStringLiteral("three-column"));
    }
    void centeredMaster()
    {
        runMatrix(QStringLiteral("centered-master"));
    }
    void cascade()
    {
        runMatrix(QStringLiteral("cascade"));
    }
    void tatami()
    {
        runMatrix(QStringLiteral("tatami"));
    }
    void spiral()
    {
        runMatrix(QStringLiteral("spiral"));
    }
    void floatingCenter()
    {
        runMatrix(QStringLiteral("floating-center"));
    }
    void cluster()
    {
        runMatrix(QStringLiteral("cluster"));
    }
    void dwindleMemory()
    {
        runMatrix(QStringLiteral("dwindle-memory"));
    }
    void bsp()
    {
        runMatrix(QStringLiteral("bsp"));
    }

    void clusterWithWindows();
    void dwindleMemoryTreePath();
};

void TestLuauParity::runMatrix(const QString& name)
{
    LuauTileAlgorithm lua(luaPath(name), m_watchdog);
    QVERIFY2(lua.isValid(), qPrintable(QStringLiteral("Luau %1 failed to load").arg(name)));

    const QList<QRect> areas = {QRect(0, 0, 1920, 1080), QRect(0, 0, 1366, 768), QRect(0, 0, 800, 1280)};
    const QList<int> gaps = {0, 8, 20};
    const QList<int> counts = {1, 2, 3, 4, 5, 6};
    const QList<qreal> ratios = {0.35, 0.6};
    const QList<int> masterCounts = {1, 2};

    const QVector<QSize> someMins = {QSize(200, 150), QSize(250, 120), QSize(180, 200),
                                     QSize(300, 100), QSize(220, 160), QSize(190, 140)};
    const QList<QVector<QSize>> minVariants = {QVector<QSize>{}, someMins};

    for (const QRect& area : areas) {
        for (int gap : gaps) {
            for (int count : counts) {
                for (qreal ratio : ratios) {
                    for (int masterCount : masterCounts) {
                        for (const QVector<QSize>& mins : minVariants) {
                            TilingState state(QStringLiteral("screen-1"));
                            for (int w = 0; w < count; ++w) {
                                state.addWindow(QStringLiteral("w%1").arg(w));
                            }
                            state.setSplitRatio(ratio);
                            state.setMasterCount(masterCount);
                            state.clearSplitTree();

                            TilingParams p;
                            p.windowCount = count;
                            p.screenGeometry = area;
                            p.state = &state;
                            p.innerGap = gap;
                            p.outerGaps = EdgeGaps::uniform(0);
                            p.minSizes = mins;

                            const QString key = QStringLiteral("%1|%2x%3|g%4|c%5|r%6|m%7|s%8")
                                                    .arg(name)
                                                    .arg(area.width())
                                                    .arg(area.height())
                                                    .arg(gap)
                                                    .arg(count)
                                                    .arg(ratio)
                                                    .arg(masterCount)
                                                    .arg(mins.size());
                            record(key, lua.calculateZones(p));
                        }
                    }
                }
            }
        }
    }
}

void TestLuauParity::clusterWithWindows()
{
    LuauTileAlgorithm lua(luaPath(QStringLiteral("cluster")), m_watchdog);
    QVERIFY(lua.isValid());

    const QList<QStringList> patterns = {
        {QStringLiteral("firefox"), QStringLiteral("firefox"), QStringLiteral("dolphin"), QStringLiteral("konsole"),
         QStringLiteral("firefox"), QStringLiteral("kate")},
        {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e"),
         QStringLiteral("f")},
        {QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("x"),
         QStringLiteral("x")},
        {QStringLiteral("p"), QStringLiteral("q"), QStringLiteral("p"), QStringLiteral("q"), QStringLiteral("p"),
         QStringLiteral("q")},
    };
    const QList<QRect> areas = {QRect(0, 0, 1920, 1080), QRect(0, 0, 800, 1280)};
    for (int pi = 0; pi < patterns.size(); ++pi) {
        const QStringList& pat = patterns.at(pi);
        for (const QRect& area : areas) {
            for (int gap : {0, 8}) {
                for (int count : {1, 2, 3, 4, 5, 6}) {
                    for (bool portrait : {false, true}) {
                        for (int focused : {-1, 0, 2}) {
                            TilingState state(QStringLiteral("s"));
                            TilingParams p;
                            p.windowCount = count;
                            p.screenGeometry = area;
                            p.state = &state;
                            p.innerGap = gap;
                            p.outerGaps = EdgeGaps::uniform(0);
                            for (int w = 0; w < count; ++w) {
                                WindowInfo wi;
                                wi.appId = pat.at(w % pat.size());
                                wi.focused = (w == focused);
                                p.windowInfos.append(wi);
                            }
                            p.focusedIndex = focused;
                            p.screenInfo.id = QStringLiteral("S1");
                            p.screenInfo.portrait = portrait;

                            const QString key = QStringLiteral("cluster-win|p%1|%2x%3|g%4|c%5|o%6|f%7")
                                                    .arg(pi)
                                                    .arg(area.width())
                                                    .arg(area.height())
                                                    .arg(gap)
                                                    .arg(count)
                                                    .arg(portrait ? 1 : 0)
                                                    .arg(focused);
                            record(key, lua.calculateZones(p));
                        }
                    }
                }
            }
        }
    }
}

void TestLuauParity::dwindleMemoryTreePath()
{
    // Structural validation of the dwindle-memory tree path: a known split tree
    // must drive the geometry — distinguished from the stateless fallback by
    // min-sizes, which the tree path ignores by design.
    LuauTileAlgorithm lua(luaPath(QStringLiteral("dwindle-memory")), m_watchdog);
    QVERIFY(lua.isValid());
    QVERIFY(lua.supportsMemory());

    TilingState state(QStringLiteral("s"));
    for (int w = 0; w < 3; ++w) {
        state.addWindow(QStringLiteral("w%1").arg(w));
    }
    state.setSplitRatio(0.5);
    auto tree = std::make_unique<SplitTree>();
    for (int w = 0; w < 3; ++w) {
        tree->insertAtEnd(QStringLiteral("w%1").arg(w), 0.5);
    }
    QCOMPARE(tree->leafCount(), 3);
    state.setSplitTree(std::move(tree));

    TilingParams p;
    p.windowCount = 3;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state;
    p.innerGap = 0;
    p.outerGaps = EdgeGaps::uniform(0);
    p.minSizes = {QSize(1000, 100), QSize(100, 100), QSize(100, 100)};

    const QVector<QRect> zones = lua.calculateZones(p);
    QCOMPARE(zones.size(), 3);
    QCOMPARE(zones[0].width(), 960); // tree path used (min-size ignored), not 1000
    const QRect area(0, 0, 1920, 1080);
    for (int i = 0; i < zones.size(); ++i) {
        QVERIFY(area.contains(zones[i]));
        for (int j = i + 1; j < zones.size(); ++j) {
            QVERIFY(!zones[i].intersects(zones[j]));
        }
    }
}

QTEST_MAIN(TestLuauParity)
#include "test_luau_parity.moc"
