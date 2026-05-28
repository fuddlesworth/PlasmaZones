// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_screenidentity_variants.cpp
 * @brief Coverage for @c ScreenIdentity::variantsFor — the consolidated
 *        connector-name ↔ resolved-id variant resolver pulled out of three
 *        duplicated call sites (discussion #461 item 12 cluster).
 *
 * The full variant-translation path requires live @c QScreen objects; with
 * the offscreen QPA there are no physical screens, so @c idForName /
 * @c nameForId fall back to "return the input verbatim" or "return empty".
 * That's enough to lock the structural contract — empty input, no
 * duplicates, input always first — without depending on a particular
 * monitor topology.
 */

#include <PhosphorScreens/ScreenIdentity.h>

#include <QTest>
#include <QStringList>

using namespace PhosphorScreens::ScreenIdentity;

class TestScreenIdentityVariants : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void emptyInput_returnsEmptyList()
    {
        // Empty input has nothing to resolve. Returning an empty list
        // lets the disable-list strip path do `for (variant : variants)
        // entries.removeAll(variant + suffix)` without a special case —
        // the loop body runs zero times.
        const QStringList variants = variantsFor(QString());
        QCOMPARE(variants.size(), 0);
    }

    void inputAlwaysFirstElement_connectorForm()
    {
        // Callers (SettingsController::setDesktopDisabled) pick variants[0]
        // for the canonical write form, so the input must come first.
        const QStringList variants = variantsFor(QStringLiteral("DP-1"));
        QVERIFY(!variants.isEmpty());
        QCOMPARE(variants.first(), QStringLiteral("DP-1"));
    }

    void inputAlwaysFirstElement_resolvedIdForm()
    {
        const QStringList variants = variantsFor(QStringLiteral("Dell:U2722D:115107"));
        QVERIFY(!variants.isEmpty());
        QCOMPARE(variants.first(), QStringLiteral("Dell:U2722D:115107"));
    }

    void offscreen_connectorForm_returnsInputOnly()
    {
        // With no live screens (offscreen QPA), idForName returns the input
        // verbatim and the de-dup branch in variantsFor folds it into a
        // single-element list. Locks the no-duplicates invariant.
        const QStringList variants = variantsFor(QStringLiteral("DP-3"));
        QCOMPARE(variants.size(), 1);
        QCOMPARE(variants.first(), QStringLiteral("DP-3"));
    }

    void offscreen_resolvedIdForm_returnsInputOnly()
    {
        // Same as above but starting from the resolved-id form. nameForId
        // returns an empty string when no screen matches, which the helper
        // must NOT append (would produce a bogus "" variant that
        // entries.removeAll would match against an empty config key).
        const QStringList variants = variantsFor(QStringLiteral("Acer:XB323U:111222"));
        QCOMPARE(variants.size(), 1);
        QCOMPARE(variants.first(), QStringLiteral("Acer:XB323U:111222"));
        QVERIFY(!variants.contains(QString()));
    }

    void noEmptyStringInOutput()
    {
        // Across a sweep of inputs that would all fail resolution under
        // the offscreen QPA, the output list must never contain "" —
        // that's the most damaging silent-failure mode for the disable-
        // list strip (it would match an empty stored key).
        const QStringList inputs = {
            QStringLiteral("HDMI-A-1"),
            QStringLiteral("eDP-1"),
            QStringLiteral("Generic:Foo:bar"),
            QStringLiteral("LG:38GN950:abcd"),
        };
        for (const QString& input : inputs) {
            const QStringList variants = variantsFor(input);
            QVERIFY2(!variants.contains(QString()),
                     qPrintable(QStringLiteral("variantsFor(%1) emitted an empty entry").arg(input)));
            QVERIFY2(!variants.isEmpty(),
                     qPrintable(QStringLiteral("variantsFor(%1) was empty for non-empty input").arg(input)));
        }
    }
};

QTEST_MAIN(TestScreenIdentityVariants)
#include "test_screenidentity_variants.moc"
