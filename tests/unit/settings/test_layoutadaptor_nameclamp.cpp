// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The 40-unit name clamp every name-producing boundary re-applies.
//
// The UI cap (QQuickTextInput::maximumLength) is advisory: a D-Bus caller can
// skip it entirely, and even the UI counts UTF-16 code units, so it can hand
// the daemon a name it has already broken. clampName is what makes the limit
// real, and the shapes below are the ones reasoning alone cannot settle. Both
// halves of a surrogate pair are code units, so a cut at 40 can land between
// them and leave a lone high surrogate that serializes as U+FFFD, which is a
// name the user never typed reaching disk.

#include "../../../src/core/constants.h"

#include <PhosphorZones/LayoutRegistry.h>

#include <QtTest>

using PlasmaZones::clampName;
using PlasmaZones::MaxLayoutNameLength;

namespace {

// U+1F600 GRINNING FACE: outside the BMP, so QString stores it as the
// surrogate pair D83D DE00 and it costs two of the clamp's 40 units.
constexpr char32_t kGrinningFaceCp = U'\U0001F600';

QString grinningFace()
{
    return QString::fromUcs4(&kGrinningFaceCp, 1);
}

QString repeated(int count)
{
    return QString(count, QLatin1Char('a'));
}

// Every surrogate in @p s is a high one immediately followed by a low one.
// This is the property the clamp exists to preserve, and asserting the exact
// result string alone would not pin it: a result can be the right length and
// still end mid-pair.
bool isValidUtf16(const QString& s)
{
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i).isHighSurrogate()) {
            if (i + 1 >= s.size() || !s.at(i + 1).isLowSurrogate()) {
                return false;
            }
            ++i;
            continue;
        }
        if (s.at(i).isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestLayoutAdaptorNameClamp : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void clampsWithinLimitUntouched();
    void cutsWholeEmojiStraddlingTheBoundary();
    void repairsLoneHighSurrogateAtExactlyTheLimit();
    void trimsWhitespaceTheCutExposes();
    void honoursReducedBudget();
};

void TestLayoutAdaptorNameClamp::clampsWithinLimitUntouched()
{
    // The overwhelmingly common case: a name nobody needs to repair comes back
    // as it went in. A clamp that mangled these would be worse than no clamp.
    QCOMPARE(clampName(QStringLiteral("Grid")), QStringLiteral("Grid"));
    QCOMPARE(clampName(QString()), QString());

    // Exactly at the limit is within it, so it is not cut.
    const QString exactly = repeated(MaxLayoutNameLength);
    QCOMPARE(clampName(exactly), exactly);

    // One unit over is.
    QCOMPARE(clampName(repeated(MaxLayoutNameLength + 1)).size(), MaxLayoutNameLength);

    // An emoji well inside the limit is not the clamp's business.
    const QString emojiName = grinningFace() + QStringLiteral(" Grid");
    QCOMPARE(clampName(emojiName), emojiName);

    // An emoji whose LOW surrogate is the 40th unit ends the name well-formed,
    // so it survives whole. This is the false-positive guard: a clamp that
    // backed off from any trailing surrogate rather than a lone HIGH one would
    // eat this emoji for nothing.
    const QString endsOnPair = repeated(MaxLayoutNameLength - 2) + grinningFace();
    QCOMPARE(endsOnPair.size(), MaxLayoutNameLength);
    QCOMPARE(clampName(endsOnPair), endsOnPair);
}

void TestLayoutAdaptorNameClamp::cutsWholeEmojiStraddlingTheBoundary()
{
    // The pair opens at unit 40 and closes at 41, so a bare left(40) keeps its
    // high surrogate and drops the low one. The whole emoji goes instead.
    const QString straddling = repeated(MaxLayoutNameLength - 1) + grinningFace();
    QCOMPARE(straddling.size(), MaxLayoutNameLength + 1);
    QVERIFY(isValidUtf16(straddling));

    const QString out = clampName(straddling);
    QVERIFY2(isValidUtf16(out), "clamp left a lone surrogate, which serializes as U+FFFD");
    QCOMPARE(out, repeated(MaxLayoutNameLength - 1));
    QVERIFY(out.size() <= MaxLayoutNameLength);
}

void TestLayoutAdaptorNameClamp::repairsLoneHighSurrogateAtExactlyTheLimit()
{
    // What the editor's own maximumLength can hand the D-Bus boundary: it
    // truncates by code units too, so a name that reaches here is already
    // broken and is exactly at the limit, not over it. A size-only fast path
    // would call this one fine and pass the lone surrogate straight through to
    // disk.
    QString loneTail = repeated(MaxLayoutNameLength - 1);
    loneTail.append(QChar(QChar::highSurrogate(kGrinningFaceCp)));
    QCOMPARE(loneTail.size(), MaxLayoutNameLength);
    QVERIFY2(!isValidUtf16(loneTail), "fixture is not the broken shape this test is about");

    const QString out = clampName(loneTail);
    QVERIFY2(isValidUtf16(out), "clamp passed an already-broken name through untouched");
    QCOMPARE(out, repeated(MaxLayoutNameLength - 1));
}

void TestLayoutAdaptorNameClamp::trimsWhitespaceTheCutExposes()
{
    // The cut can land mid-word and leave the space before it dangling, so the
    // stored name ends in whitespace the user cannot see.
    const QString cutAtSpace = repeated(MaxLayoutNameLength - 1) + QStringLiteral("  tail");
    const QString out = clampName(cutAtSpace);
    QVERIFY(!out.endsWith(QLatin1Char(' ')));
    QCOMPARE(out, repeated(MaxLayoutNameLength - 1));

    // A whole run of exposed whitespace goes, not just the last one.
    const QString cutInRun = repeated(MaxLayoutNameLength - 2) + QStringLiteral("   x");
    QCOMPARE(clampName(cutInRun), repeated(MaxLayoutNameLength - 2));
}

void TestLayoutAdaptorNameClamp::honoursReducedBudget()
{
    // duplicateLayout re-appends the registry's suffix after clamping, so it
    // clamps the BASE to a reduced budget and the two together still fit. The
    // suffix is read from the registry rather than spelled here, so this tracks
    // the caller instead of drifting from it.
    const QString suffix = PhosphorZones::LayoutRegistry::duplicateNameSuffix();
    QVERIFY(!suffix.isEmpty());
    const int budget = MaxLayoutNameLength - suffix.size();
    QVERIFY(budget > 0);

    const QString clamped = clampName(repeated(MaxLayoutNameLength - 1), budget) + suffix;
    QCOMPARE(clamped.size(), MaxLayoutNameLength);
    QVERIFY(clamped.endsWith(suffix));

    // The surrogate back-off is the clamp's, not the default budget's: a pair
    // straddling the REDUCED cut is repaired the same way.
    const QString emojiBase = repeated(budget - 1) + grinningFace();
    const QString emojiOut = clampName(emojiBase, budget);
    QVERIFY2(isValidUtf16(emojiOut), "reduced-budget cut split a surrogate pair");
    QCOMPARE(emojiOut, repeated(budget - 1));

    // A budget with no room for a name yields an empty one. QString::left()
    // compares its argument as size_t, so a negative length that reached it
    // would wrap and hand back the whole name unclamped.
    QCOMPARE(clampName(repeated(MaxLayoutNameLength - 1), 0), QString());
    QCOMPARE(clampName(repeated(MaxLayoutNameLength - 1), -1), QString());
}

QTEST_GUILESS_MAIN(TestLayoutAdaptorNameClamp)
#include "test_layoutadaptor_nameclamp.moc"
