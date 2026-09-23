// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the shared pluau helper functions added during the algorithm
// DRY pass: guardArea, stripLayout, resizeRatioGrow/Shrink, clamp, minSizeAt,
// gridShape, cumulativeOffsets, center, masterStackResize. Each helper is exercised directly by
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
    QFile f(QStringLiteral(PHOSPHOR_SOURCE_DIR "/libs/phosphor-tiles/src/pluau/pluau.luau"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}
} // namespace

// QVariantMap::value() hands back a default-constructed QVariant for a key that
// is not there, so toBool() yields false, toInt() yields 0 and toString() yields
// empty. Every assertion below that expects one of those values would therefore
// pass just as happily against a probe table that never set the field, which is
// what a rename in pluau.luau or a typo in a probe body produces. QVERIFY on
// isEmpty() does not help: it only proves SOME key came back.
//
// So each slot names the exact keys it is about to read. QVERIFY2 expands to a
// return, so a missing key aborts that slot naming the field rather than
// reporting a confusing value mismatch further down.
#define VERIFY_KEYS(map, ...)                                                                                          \
    do {                                                                                                               \
        for (const char* _key : {__VA_ARGS__}) {                                                                       \
            QVERIFY2((map).contains(QLatin1String(_key)), _key);                                                       \
        }                                                                                                              \
    } while (false)

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
        QVERIFY2(!prelude.isEmpty(), "pluau.luau prelude missing at PHOSPHOR_SOURCE_DIR");
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
    void masterStackResize();
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
            local tiny = { x = 5, y = 7, width = 0, height = 1000 }
            local zero = pluau.guardArea(big, 0)
            local small = pluau.guardArea(tiny, 3)
            local proceed = pluau.guardArea(big, 3)
            return {
                zeroIsNil = zero == nil, zeroLen = zero and #zero or -1,
                smallIsNil = small == nil, smallLen = small and #small or -1,
                smallW = (small and small[1] and small[1].width) or -1,
                smallX = (small and small[1] and small[1].x) or -1,
                proceedIsNil = proceed == nil,
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "zeroIsNil", "zeroLen", "smallIsNil", "smallLen", "smallW", "smallX", "proceedIsNil");
    QCOMPARE(r.value(QStringLiteral("zeroIsNil")).toBool(), false);
    QCOMPARE(r.value(QStringLiteral("zeroLen")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("smallIsNil")).toBool(), false);
    QCOMPARE(r.value(QStringLiteral("smallLen")).toInt(), 3);
    // width 0, so this is the assertion that actually exercises fillArea's
    // max(1, width) clamp rather than just echoing the input back.
    QCOMPARE(r.value(QStringLiteral("smallW")).toInt(), 1);
    // And the fallback must carry the host rect's origin, not reset to 0.
    QCOMPARE(r.value(QStringLiteral("smallX")).toInt(), 5);
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
    VERIFY_KEYS(r, "vertLen", "vertW", "vertX", "horizLen", "horizH", "horizY", "degenLen", "degenH1", "degenH2",
                "degenH3", "degenX", "degenW");
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
                grow = pluau.resizeRatioGrow(120, 100, 0.25),
                shrink = pluau.resizeRatioShrink(120, 100, 0.25),
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "grow", "shrink");
    // 0.25, deliberately NOT 0.5. grow uses oldRatio and shrink uses
    // (1 - oldRatio), so at exactly 0.5 the two closed forms compute the same
    // number and swapping the functions leaves the assertions green. 0.25
    // separates them: 0.3 against 0.1.
    QCOMPARE(r.value(QStringLiteral("grow")).toDouble(), 0.3); // 120 * 0.25 / 100
    QCOMPARE(r.value(QStringLiteral("shrink")).toDouble(), 0.1); // 1 - 120 * (1 - 0.25) / 100
}

void TestPluauHelpers::masterStackResize()
{
    // Shared master/stack resize glue. horizontal=false is the width axis (right
    // grows the master, left shrinks the stack); horizontal=true is the height
    // axis (bottom grows, top shrinks). Guards: count<=masterCount, zero old
    // dimension, or out-of-range oldRatio all return nil.
    const QByteArray body = R"LUA(
        return { run = function()
            local function ev(idx, oldRect, newRect, edges)
                return { index = idx, oldRect = oldRect, newRect = newRect, edges = edges }
            end
            local noEdge = { left = false, right = false, top = false, bottom = false }
            local function edge(k)
                local e = { left = false, right = false, top = false, bottom = false }
                e[k] = true
                return e
            end
            -- Vertical seam (width axis): master (idx 0) right edge grows.
            local vGrow = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(0, { width = 100, height = 80 }, { width = 120, height = 80 }, edge("right")),
                false)
            -- Vertical seam: stack (idx 1) left edge shrinks.
            local vShrink = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(1, { width = 100, height = 80 }, { width = 120, height = 80 }, edge("left")),
                false)
            -- Horizontal seam (height axis): master bottom edge grows.
            local hGrow = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(0, { width = 100, height = 80 }, { width = 100, height = 100 }, edge("bottom")),
                true)
            -- Horizontal seam: stack top edge shrinks.
            local hShrink = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(1, { width = 100, height = 80 }, { width = 100, height = 100 }, edge("top")),
                true)
            -- count <= masterCount → no seam → nil.
            local single = pluau.masterStackResize(
                { windowCount = 1, masterCount = 1, splitRatio = 0.25 },
                ev(0, { width = 100, height = 80 }, { width = 120, height = 80 }, edge("right")),
                false)
            -- Out-of-range oldRatio (>= 1) → nil.
            local badRatio = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 1.0 },
                ev(0, { width = 100, height = 80 }, { width = 120, height = 80 }, edge("right")),
                false)
            -- Zero old dimension → nil.
            local zeroDim = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(0, { width = 0, height = 80 }, { width = 120, height = 80 }, edge("right")),
                false)
            -- Correct index but wrong edge → nil.
            local wrongEdge = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(0, { width = 100, height = 80 }, { width = 120, height = 80 }, noEdge),
                false)
            return {
                vGrow = vGrow and vGrow.splitRatio or -1,
                vShrink = vShrink and vShrink.splitRatio or -1,
                hGrow = hGrow and hGrow.splitRatio or -1,
                hShrink = hShrink and hShrink.splitRatio or -1,
                singleIsNil = single == nil,
                badRatioIsNil = badRatio == nil,
                zeroDimIsNil = zeroDim == nil,
                wrongEdgeIsNil = wrongEdge == nil,
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "vGrow", "vShrink", "hGrow", "hShrink", "singleIsNil", "badRatioIsNil", "zeroDimIsNil",
                "wrongEdgeIsNil");
    // The old rect is 100x80, not square, and the ratio is 0.25. Both matter.
    // A square rect hides an axis mix-up confined to oldDim, and ratio 0.5
    // makes the grow and shrink closed forms compute the same number. With
    // these inputs the width axis gives 0.3/0.1 and the height axis gives
    // 0.3125/0.0625, so swapping either the axis or the two forms fails.
    QCOMPARE(r.value(QStringLiteral("vGrow")).toDouble(), 0.3); // 120 * 0.25 / 100
    QCOMPARE(r.value(QStringLiteral("vShrink")).toDouble(), 0.1); // 1 - 120 * 0.75 / 100
    QCOMPARE(r.value(QStringLiteral("hGrow")).toDouble(), 0.3125); // 100 * 0.25 / 80
    QCOMPARE(r.value(QStringLiteral("hShrink")).toDouble(), 0.0625); // 1 - 100 * 0.75 / 80
    QCOMPARE(r.value(QStringLiteral("singleIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("badRatioIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("zeroDimIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("wrongEdgeIsNil")).toBool(), true);
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
    VERIFY_KEYS(r, "mid", "lo", "hi", "nanIsNil", "strIsNil");
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
    VERIFY_KEYS(r, "w0", "h0", "w1", "h1", "w5", "h5", "we", "he");
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
            local c0, r0 = pluau.gridShape(0)
            return { c1 = c1, r1 = r1, c4 = c4, r4 = r4, c5 = c5, r5 = r5, c9 = c9, r9 = r9,
                     c0 = c0, r0 = r0 }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "c1", "r1", "c4", "r4", "c5", "r5", "c9", "r9", "c0", "r0");
    QCOMPARE(r.value(QStringLiteral("c1")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("r1")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("c4")).toInt(), 2);
    QCOMPARE(r.value(QStringLiteral("r4")).toInt(), 2);
    QCOMPARE(r.value(QStringLiteral("c5")).toInt(), 3); // ceil(sqrt(5)) = 3
    QCOMPARE(r.value(QStringLiteral("r5")).toInt(), 2); // ceil(5 / 3) = 2
    QCOMPARE(r.value(QStringLiteral("c9")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("r9")).toInt(), 3);
    // count 0 gives 1 x 0. What matters is that it is not 0 x ceil(0/0):
    // cols was 0, so rows came back nan and propagated silently into any
    // caller that did not guard first. All three bundled callers do
    // guard, but a user script need not.
    QCOMPARE(r.value(QStringLiteral("c0")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("r0")).toInt(), 0);
}

void TestPluauHelpers::cumulativeOffsets()
{
    const QByteArray body = R"LUA(
        return { run = function()
            local o = pluau.cumulativeOffsets(100, { 50, 60, 70 }, 10)
            local e = pluau.cumulativeOffsets(100, {}, 10)
            return { len = #o, o1 = o[1], o2 = o[2], o3 = o[3], elen = #e, e1 = e[1] }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "len", "o1", "o2", "o3", "elen", "e1");
    QCOMPARE(r.value(QStringLiteral("len")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("o1")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("o2")).toInt(), 160); // 100 + 50 + 10
    QCOMPARE(r.value(QStringLiteral("o3")).toInt(), 230); // 160 + 60 + 10
    // Documented empty-input contract: an empty sizes list still yields { start }.
    QCOMPARE(r.value(QStringLiteral("elen")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("e1")).toInt(), 100);
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
    VERIFY_KEYS(r, "a", "b");
    QCOMPARE(r.value(QStringLiteral("a")).toInt(), 30); // 0 + floor((100 - 40) / 2)
    QCOMPARE(r.value(QStringLiteral("b")).toInt(), 39); // 10 + floor((100 - 41) / 2)
}

#undef VERIFY_KEYS

QTEST_MAIN(TestPluauHelpers)
#include "test_pluau_helpers.moc"
