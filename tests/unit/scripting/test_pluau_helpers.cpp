// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the shared pluau helper functions added during the algorithm
// DRY pass: guardArea, stripLayout, resizeRatioGrow/Shrink, clamp, minSizeAt,
// gridShape, cumulativeOffsets, center. Each helper is exercised directly by
// loading the real pluau.luau prelude into a sandboxed Luau VM (the same way the
// production loader injects it) and calling the function from a probe module.
//
// Behaviour parity of the *algorithms* that now consume these helpers is covered
// separately by the golden-snapshot harness (test_luau_parity.cpp); this file
// pins the helpers themselves.

#include <QtTest>

#include <QByteArray>
#include <QFile>

#include <PhosphorScripting/LuauEngine.h>

#include <memory>

using namespace PhosphorScripting;

namespace {
QByteArray preludeSource()
{
    QFile f(QStringLiteral(P_SOURCE_DIR "/libs/phosphor-tiles/src/pluau/pluau.luau"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}
} // namespace

class TestPluauHelpers : public QObject
{
    Q_OBJECT

    std::unique_ptr<LuauEngine> m_engine;

    // Load a probe module that defines run(), call it, and hand back the result.
    QVariant run(const QByteArray& body)
    {
        QString err;
        const int h = m_engine->loadModule(QStringLiteral("probe"), body, &err);
        if (h < 0) {
            qWarning().noquote() << "probe failed to load:" << err;
            return {};
        }
        const auto out = m_engine->callModule(h, QStringLiteral("run"), {}, 500);
        m_engine->releaseModule(h);
        if (out.status != LuauEngine::CallStatus::Ok) {
            qWarning().noquote() << "probe run failed:" << out.message;
            return {};
        }
        return out.result;
    }

private Q_SLOTS:
    void init()
    {
        m_engine = std::make_unique<LuauEngine>();
        QVERIFY(m_engine->init());
        const QByteArray prelude = preludeSource();
        QVERIFY2(!prelude.isEmpty(), "pluau.luau prelude missing at P_SOURCE_DIR");
        QString err;
        QVERIFY2(m_engine->runPrelude(QStringLiteral("pluau"), prelude, &err), qPrintable(err));
        m_engine->sandbox();
    }

    void cleanup()
    {
        m_engine.reset();
    }

    void guardArea();
    void stripLayoutEvenAndDegenerate();
    void resizeRatio();
    void clampBoundsAndNaN();
    void minSizeAt();
    void gridShape();
    void cumulativeOffsets();
    void center();
};

void TestPluauHelpers::guardArea()
{
    // count <= 0 → empty list; sub-minimum area → host fill fallback; otherwise nil.
    const QByteArray body = R"LUA(
        return { run = function()
            local big = { x = 0, y = 0, width = 1000, height = 1000 }
            local tiny = { x = 5, y = 7, width = 10, height = 1000 }
            local zero = pluau.guardArea(big, 0)
            local small = pluau.guardArea(tiny, 3)
            local proceed = pluau.guardArea(big, 3)
            return {
                zeroIsNil = zero == nil, zeroLen = zero and #zero or -1,
                smallIsNil = small == nil, smallLen = small and #small or -1,
                smallW = (small and small[1] and small[1].width) or -1,
                proceedIsNil = proceed == nil,
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    QCOMPARE(r.value(QStringLiteral("zeroIsNil")).toBool(), false);
    QCOMPARE(r.value(QStringLiteral("zeroLen")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("smallIsNil")).toBool(), false);
    QCOMPARE(r.value(QStringLiteral("smallLen")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("smallW")).toInt(), 10); // fillArea uses max(1, width)
    QCOMPARE(r.value(QStringLiteral("proceedIsNil")).toBool(), true);
}

void TestPluauHelpers::stripLayoutEvenAndDegenerate()
{
    const QByteArray body = R"LUA(
        return { run = function()
            local vert = {}
            pluau.stripLayout(vert, 0, 0, 100, 300, 3, 10, false)
            local horiz = {}
            pluau.stripLayout(horiz, 0, 0, 300, 100, 3, 10, true)
            local degen = {}
            pluau.stripLayout(degen, 0, 0, 100, 20, 3, 20, false)
            return {
                vertLen = #vert, vertW = vert[1].width, vertX = vert[1].x,
                horizLen = #horiz, horizH = horiz[1].height, horizY = horiz[1].y,
                degenLen = #degen,
                degenH1 = degen[1].height, degenH2 = degen[2].height, degenH3 = degen[3].height,
                degenX = degen[1].x, degenW = degen[1].width,
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    // Vertical strip: fixed width = panelW, fixed x = startX, height distributed.
    QCOMPARE(r.value(QStringLiteral("vertLen")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("vertW")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("vertX")).toInt(), 0);
    // Horizontal strip: fixed height = panelH, fixed y = startY, width distributed.
    QCOMPARE(r.value(QStringLiteral("horizLen")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("horizH")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("horizY")).toInt(), 0);
    // Degenerate gap ((count-1)*gap >= totalSize): equal, overlapping fills of
    // floor(totalSize/count) = floor(20/3) = 6, all anchored at (startX, startY).
    QCOMPARE(r.value(QStringLiteral("degenLen")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("degenH1")).toInt(), 6);
    QCOMPARE(r.value(QStringLiteral("degenH2")).toInt(), 6);
    QCOMPARE(r.value(QStringLiteral("degenH3")).toInt(), 6);
    QCOMPARE(r.value(QStringLiteral("degenX")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("degenW")).toInt(), 100);
}

void TestPluauHelpers::resizeRatio()
{
    const QByteArray body = R"LUA(
        return { run = function()
            return {
                grow = pluau.resizeRatioGrow(120, 100, 0.5),
                shrink = pluau.resizeRatioShrink(120, 100, 0.5),
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    QCOMPARE(r.value(QStringLiteral("grow")).toDouble(), 0.6); // 120 * 0.5 / 100
    QCOMPARE(r.value(QStringLiteral("shrink")).toDouble(), 0.4); // 1 - 120 * 0.5 / 100
}

void TestPluauHelpers::clampBoundsAndNaN()
{
    const QByteArray body = R"LUA(
        return { run = function()
            return {
                mid = pluau.clamp(0.5, 0.1, 0.9),
                lo = pluau.clamp(0.05, 0.1, 0.9),
                hi = pluau.clamp(2.0, 0.1, 0.9),
                nanIsNil = pluau.clamp(0 / 0, 0.1, 0.9) == nil,
                strIsNil = pluau.clamp("x", 0.1, 0.9) == nil,
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    QCOMPARE(r.value(QStringLiteral("mid")).toDouble(), 0.5);
    QCOMPARE(r.value(QStringLiteral("lo")).toDouble(), 0.1);
    QCOMPARE(r.value(QStringLiteral("hi")).toDouble(), 0.9);
    QCOMPARE(r.value(QStringLiteral("nanIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("strIsNil")).toBool(), true);
}

void TestPluauHelpers::minSizeAt()
{
    const QByteArray body = R"LUA(
        return { run = function()
            local ms = { { w = 200, h = 150 }, { w = 0, h = 100 } }
            local w0, h0 = pluau.minSizeAt(ms, 0)
            local w1, h1 = pluau.minSizeAt(ms, 1)
            local w5, h5 = pluau.minSizeAt(ms, 5)
            local we, he = pluau.minSizeAt({}, 0)
            return { w0 = w0, h0 = h0, w1 = w1, h1 = h1, w5 = w5, h5 = h5, we = we, he = he }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    QCOMPARE(r.value(QStringLiteral("w0")).toInt(), 200);
    QCOMPARE(r.value(QStringLiteral("h0")).toInt(), 150);
    QCOMPARE(r.value(QStringLiteral("w1")).toInt(), 0); // w = 0 is not > 0
    QCOMPARE(r.value(QStringLiteral("h1")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("w5")).toInt(), 0); // index past the array
    QCOMPARE(r.value(QStringLiteral("h5")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("we")).toInt(), 0); // empty minSizes
    QCOMPARE(r.value(QStringLiteral("he")).toInt(), 0);
}

void TestPluauHelpers::gridShape()
{
    const QByteArray body = R"LUA(
        return { run = function()
            local c1, r1 = pluau.gridShape(1)
            local c4, r4 = pluau.gridShape(4)
            local c5, r5 = pluau.gridShape(5)
            local c9, r9 = pluau.gridShape(9)
            return { c1 = c1, r1 = r1, c4 = c4, r4 = r4, c5 = c5, r5 = r5, c9 = c9, r9 = r9 }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    QCOMPARE(r.value(QStringLiteral("c1")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("r1")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("c4")).toInt(), 2);
    QCOMPARE(r.value(QStringLiteral("r4")).toInt(), 2);
    QCOMPARE(r.value(QStringLiteral("c5")).toInt(), 3); // ceil(sqrt(5)) = 3
    QCOMPARE(r.value(QStringLiteral("r5")).toInt(), 2); // ceil(5 / 3) = 2
    QCOMPARE(r.value(QStringLiteral("c9")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("r9")).toInt(), 3);
}

void TestPluauHelpers::cumulativeOffsets()
{
    const QByteArray body = R"LUA(
        return { run = function()
            local o = pluau.cumulativeOffsets(100, { 50, 60, 70 }, 10)
            return { len = #o, o1 = o[1], o2 = o[2], o3 = o[3] }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    QCOMPARE(r.value(QStringLiteral("len")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("o1")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("o2")).toInt(), 160); // 100 + 50 + 10
    QCOMPARE(r.value(QStringLiteral("o3")).toInt(), 230); // 160 + 60 + 10
}

void TestPluauHelpers::center()
{
    const QByteArray body = R"LUA(
        return { run = function()
            return {
                a = pluau.center(0, 100, 40),
                b = pluau.center(10, 100, 41),
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    QVERIFY(!r.isEmpty());
    QCOMPARE(r.value(QStringLiteral("a")).toInt(), 30); // 0 + floor((100 - 40) / 2)
    QCOMPARE(r.value(QStringLiteral("b")).toInt(), 39); // 10 + floor((100 - 41) / 2)
}

QTEST_MAIN(TestPluauHelpers)
#include "test_pluau_helpers.moc"
