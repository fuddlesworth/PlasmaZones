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
            -- startX 11 and startY 23 are deliberately DISTINCT and non-zero.
            -- With (0, 0) the fixed and distributed axes both start at 0, so
            -- swapping x for y in the fixed axis produces identical output and
            -- ships green.
            local vert = {}
            pluau.stripLayout(vert, 11, 23, 100, 300, 3, 10, false)
            local horiz = {}
            pluau.stripLayout(horiz, 11, 23, 300, 100, 3, 10, true)
            local degen = {}
            pluau.stripLayout(degen, 11, 23, 100, 20, 3, 20, false)
            -- The degenerate leg above is strictly past the boundary
            -- ((count-1)*gap = 40 > 20), so it does not pin WHICH side of
            -- `>=` the equality falls on. This one sits exactly on it:
            -- (3-1)*150 == 300. `>` was the only mutation of this branch that
            -- survived the three legs above; this leg closes it.
            local bound = {}
            pluau.stripLayout(bound, 11, 23, 100, 300, 3, 150, false)
            -- count == 1 is the ONLY shape the only production consumer
            -- produces: floating-center.luau's two side-strip calls are each
            -- guarded by `> 0` on a count that can only be 0 or 1. So the
            -- multi-slot legs above have no production caller, and this one
            -- carries the whole real path. It must be an exact identity.
            local one = {}
            pluau.stripLayout(one, 11, 23, 100, 300, 1, 10, false)
            return {
                vertLen = #vert, vertW = vert[1].width, vertX = vert[1].x,
                vertY1 = vert[1].y, vertY2 = vert[2].y, vertY3 = vert[3].y,
                vertH1 = vert[1].height, vertH3 = vert[3].height,
                horizLen = #horiz, horizH = horiz[1].height, horizY = horiz[1].y,
                horizX1 = horiz[1].x, horizX2 = horiz[2].x, horizX3 = horiz[3].x,
                horizW1 = horiz[1].width, horizW3 = horiz[3].width,
                degenLen = #degen,
                degenH1 = degen[1].height, degenH2 = degen[2].height, degenH3 = degen[3].height,
                degenX = degen[1].x, degenY = degen[1].y, degenW = degen[1].width,
                boundH1 = bound[1].height, boundH2 = bound[2].height, boundH3 = bound[3].height,
                boundY1 = bound[1].y, boundY2 = bound[2].y, boundY3 = bound[3].y,
                oneLen = #one, oneX = one[1].x, oneY = one[1].y, oneW = one[1].width, oneH = one[1].height,
            }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "vertLen", "vertW", "vertX", "vertY1", "vertY2", "vertY3", "vertH1", "vertH3", "horizLen", "horizH",
                "horizY", "horizX1", "horizX2", "horizX3", "horizW1", "horizW3", "degenLen", "degenH1", "degenH2",
                "degenH3", "degenX", "degenY", "degenW", "boundH1", "boundH2", "boundH3", "boundY1", "boundY2",
                "boundY3", "oneLen", "oneX", "oneY", "oneW", "oneH");
    // Vertical strip: fixed width = panelW, fixed x = startX, height distributed
    // down from startY. 300 less two 10px gaps is 280; 280/3 is 93 remainder 1,
    // and the remainder lands on the LAST cell, so heights are 93, 93, 94 and
    // the y origins are 23, 23+93+10, 23+93+10+93+10.
    QCOMPARE(r.value(QStringLiteral("vertLen")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("vertW")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("vertX")).toInt(), 11);
    QCOMPARE(r.value(QStringLiteral("vertY1")).toInt(), 23);
    QCOMPARE(r.value(QStringLiteral("vertY2")).toInt(), 126);
    QCOMPARE(r.value(QStringLiteral("vertY3")).toInt(), 229);
    QCOMPARE(r.value(QStringLiteral("vertH1")).toInt(), 93);
    QCOMPARE(r.value(QStringLiteral("vertH3")).toInt(), 94);
    // Horizontal strip: the exact mirror, with the same remainder rule on x.
    QCOMPARE(r.value(QStringLiteral("horizLen")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("horizH")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("horizY")).toInt(), 23);
    QCOMPARE(r.value(QStringLiteral("horizX1")).toInt(), 11);
    QCOMPARE(r.value(QStringLiteral("horizX2")).toInt(), 114);
    QCOMPARE(r.value(QStringLiteral("horizX3")).toInt(), 217);
    QCOMPARE(r.value(QStringLiteral("horizW1")).toInt(), 93);
    QCOMPARE(r.value(QStringLiteral("horizW3")).toInt(), 94);
    // Degenerate gap ((count-1)*gap >= totalSize): equal, overlapping fills of
    // floor(totalSize/count) = floor(20/3) = 6, all anchored at (startX, startY).
    QCOMPARE(r.value(QStringLiteral("degenLen")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("degenH1")).toInt(), 6);
    QCOMPARE(r.value(QStringLiteral("degenH2")).toInt(), 6);
    QCOMPARE(r.value(QStringLiteral("degenH3")).toInt(), 6);
    QCOMPARE(r.value(QStringLiteral("degenX")).toInt(), 11);
    QCOMPARE(r.value(QStringLiteral("degenY")).toInt(), 23);
    QCOMPARE(r.value(QStringLiteral("degenW")).toInt(), 100);
    // Exactly ON the boundary the guard spells `>=`: equality takes the
    // DEGENERATE branch, so all three are full-height fills stacked at startY.
    // With `>` it would fall through to the even branch and give three 1px
    // slivers at y 23/172/321 instead, which is what this pins.
    QCOMPARE(r.value(QStringLiteral("boundH1")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("boundH2")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("boundH3")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("boundY1")).toInt(), 23);
    QCOMPARE(r.value(QStringLiteral("boundY2")).toInt(), 23);
    QCOMPARE(r.value(QStringLiteral("boundY3")).toInt(), 23);
    // Exact identity: one zone, the panel verbatim. distributeEvenly takes its
    // count==1 early return here, and mutating that return changes 16446 of
    // 31680 floating-center configurations, all of which shipped green before
    // this leg existed.
    QCOMPARE(r.value(QStringLiteral("oneLen")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("oneX")).toInt(), 11);
    QCOMPARE(r.value(QStringLiteral("oneY")).toInt(), 23);
    QCOMPARE(r.value(QStringLiteral("oneW")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("oneH")).toInt(), 300);
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
    // What separates grow from shrink here is newSize != oldSize, NOT the
    // ratio. grow = n*r/o and shrink = 1 - n*(1-r)/o are equal exactly when
    // n*r/o + n*(1-r)/o == 1, i.e. when n == o; the ratio cancels. So 120
    // against 100 is the load-bearing part and any ratio would do. Measured
    // on the real prelude: at n == o they collide at 0.25, 0.5 and 0.75
    // alike, and at 120/100 they never collide.
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
            -- The two SEAM-SIDE mismatches. Each index owns exactly one edge of
            -- the seam: the master drives it from the right, the stack from the
            -- left. Giving a valid index the OTHER side's edge must be refused,
            -- and neither leg was covered -- wrongEdge above passes no edge at
            -- all, which a "did any edge flag get set" check would also accept.
            local masterWrongSide = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(0, { width = 100, height = 80 }, { width = 120, height = 80 }, edge("left")),
                false)
            local stackWrongSide = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.25 },
                ev(1, { width = 100, height = 80 }, { width = 120, height = 80 }, edge("right")),
                false)
            -- Out-of-range oldRatio at the LOW end. badRatio above only covers
            -- >= 1, so a guard written `oldRatio >= 1` instead of
            -- `oldRatio <= 0 or oldRatio >= 1` shipped green.
            local zeroRatio = pluau.masterStackResize(
                { windowCount = 3, masterCount = 1, splitRatio = 0.0 },
                ev(0, { width = 100, height = 80 }, { width = 120, height = 80 }, edge("right")),
                false)
            return {
                masterWrongSideIsNil = masterWrongSide == nil,
                stackWrongSideIsNil = stackWrongSide == nil,
                zeroRatioIsNil = zeroRatio == nil,
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
                "wrongEdgeIsNil", "masterWrongSideIsNil", "stackWrongSideIsNil", "zeroRatioIsNil");
    // The old rect is 100x80, deliberately NOT square: a square one hides an
    // axis mix-up confined to oldDim, because both axes then produce the
    // same number. Here the width axis gives 0.3/0.1 and the height axis
    // 0.3125/0.0625, so inverting `horizontal` fails.
    //
    // grow and shrink are told apart by newSize != oldSize rather than by
    // the ratio (see resizeRatio above for the algebra), which both axes
    // satisfy: 120 against 100, and 100 against 80.
    QCOMPARE(r.value(QStringLiteral("vGrow")).toDouble(), 0.3); // 120 * 0.25 / 100
    // 1 - 120 * 0.75 / 100. Exact in decimal, NOT in binary: 0.9 has no exact
    // double, so this arrives as 0.09999999999999998. QCOMPARE on doubles is
    // qFuzzyCompare, which is why the literal 0.1 passes. Do not "tighten"
    // this to a bitwise compare.
    QCOMPARE(r.value(QStringLiteral("vShrink")).toDouble(), 0.1);
    QCOMPARE(r.value(QStringLiteral("hGrow")).toDouble(), 0.3125); // 100 * 0.25 / 80
    QCOMPARE(r.value(QStringLiteral("hShrink")).toDouble(), 0.0625); // 1 - 100 * 0.75 / 80
    QCOMPARE(r.value(QStringLiteral("singleIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("badRatioIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("zeroDimIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("wrongEdgeIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("masterWrongSideIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("stackWrongSideIsNil")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("zeroRatioIsNil")).toBool(), true);
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
            -- A NEGATIVE entry: w1 above uses 0, which the `and entry.w or 0`
            -- fallback already yields, so it cannot tell the `> 0` filter from
            -- no filter at all. -5 can.
            local wNeg, hNeg = pluau.minSizeAt({ { w = -5, h = -7 } }, 0)
            -- A SPARSE array, where #ms stops short of a populated index. w5
            -- above reads past a DENSE array, which yields nil and is caught by
            -- the `and entry` term, so it exercises the nil path rather than
            -- the `i < #minSizes` bound. This does.
            local sparse = {}
            sparse[1] = { w = 10, h = 10 }
            sparse[3] = { w = 99, h = 99 }
            local wSp, hSp = pluau.minSizeAt(sparse, 2)
            return { w0 = w0, h0 = h0, w1 = w1, h1 = h1, w5 = w5, h5 = h5, we = we, he = he,
                     wNeg = wNeg, hNeg = hNeg, wSp = wSp, hSp = hSp }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "w0", "h0", "w1", "h1", "w5", "h5", "we", "he", "wNeg", "hNeg", "wSp", "hSp");
    QCOMPARE(r.value(QStringLiteral("w0")).toInt(), 200);
    QCOMPARE(r.value(QStringLiteral("h0")).toInt(), 150);
    QCOMPARE(r.value(QStringLiteral("w1")).toInt(), 0); // w = 0 is not > 0
    QCOMPARE(r.value(QStringLiteral("h1")).toInt(), 100);
    QCOMPARE(r.value(QStringLiteral("w5")).toInt(), 0); // index past the array
    QCOMPARE(r.value(QStringLiteral("h5")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("we")).toInt(), 0); // empty minSizes
    QCOMPARE(r.value(QStringLiteral("he")).toInt(), 0);
    // Without the `> 0` filter these would be -5 / -7 and a negative minimum
    // would propagate into the layout arithmetic.
    QCOMPARE(r.value(QStringLiteral("wNeg")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("hNeg")).toInt(), 0);
    // Without the `i < #minSizes` bound this would read sparse[3] and return
    // 99 / 99 for an index the array does not densely cover.
    QCOMPARE(r.value(QStringLiteral("wSp")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("hSp")).toInt(), 0);
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
            local cNeg, rNeg = pluau.gridShape(-1)
            return { c1 = c1, r1 = r1, c4 = c4, r4 = r4, c5 = c5, r5 = r5, c9 = c9, r9 = r9,
                     c0 = c0, r0 = r0, r0IsNan = r0 ~= r0,
                     cNeg = cNeg, rNeg = rNeg, cNegIsNan = cNeg ~= cNeg, rNegIsNan = rNeg ~= rNeg }
        end }
    )LUA";
    const QVariantMap r = run(body).toMap();
    VERIFY_KEYS(r, "c1", "r1", "c4", "r4", "c5", "r5", "c9", "r9", "c0", "r0", "r0IsNan", "cNeg", "rNeg", "cNegIsNan",
                "rNegIsNan");
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
    // Asserted in Luau, not through QVariant: QVariant(double NaN).toInt() is
    // 0, and the marshaller only takes its whole-number branch for finite
    // values, so the r0 compare above would have passed against the old
    // 0 x nan too. This is the leg that actually pins "not nan".
    QCOMPARE(r.value(QStringLiteral("r0IsNan")).toBool(), false);
    // NEGATIVE count is what actually pins the max() argument order, and count
    // 0 above does not: sqrt(0) is 0, so max(1, 0) and max(0, 1) both give 1
    // and the shape is 1 x 0 either way. Only below zero does sqrt return nan,
    // where max(1, nan) is 1 but max(nan, 1) is nan. Measured on the real
    // prelude: gridShape(-1) is (1, -1) as written and (nan, nan) with the
    // arguments swapped. Without this leg a swap ships green.
    QCOMPARE(r.value(QStringLiteral("cNeg")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("rNeg")).toInt(), -1);
    QCOMPARE(r.value(QStringLiteral("cNegIsNan")).toBool(), false);
    QCOMPARE(r.value(QStringLiteral("rNegIsNan")).toBool(), false);
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
