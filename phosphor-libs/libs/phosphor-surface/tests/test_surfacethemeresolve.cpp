// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceThemeResolve.h>
#include <PhosphorSurface/SurfaceWindowStateResolve.h>

#include <QtTest/QtTest>

using namespace PhosphorSurfaceShaders;

namespace {
SurfaceShaderEffect::ParameterInfo parameter(const QString& id, const QString& type, const QVariant& value)
{
    SurfaceShaderEffect::ParameterInfo result;
    result.id = id;
    result.type = type;
    result.defaultValue = value;
    return result;
}

SurfaceShaderEffect identityPack()
{
    SurfaceShaderEffect effect;
    effect.parameters = {
        parameter(QStringLiteral("useWindowAccent"), QStringLiteral("bool"), true),
        parameter(QStringLiteral("activeColor"), QStringLiteral("color"), QStringLiteral("#b3223344")),
        parameter(QStringLiteral("inactiveColor"), QStringLiteral("color"), QStringLiteral("#66223344")),
        parameter(QStringLiteral("glowColor"), QStringLiteral("color"), QStringLiteral("#80223344")),
    };
    return effect;
}

SurfaceThemeColors theme()
{
    return {QColor(QStringLiteral("#ffbb3300")), QColor(QStringLiteral("#ff557799")),
            QColor(QStringLiteral("#ff111111")), QColor(QStringLiteral("#ffffffff")),
            QColor(QStringLiteral("#ff12ab56"))};
}
}

class TestSurfaceThemeResolve : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void windowIdentityKeepsEachDefaultAlpha()
    {
        QVariantMap values;
        resolveThemeParamColors(identityPack(), values, theme());
        QCOMPARE(values.value(QStringLiteral("activeColor")).value<QColor>(), QColor(QStringLiteral("#b312ab56")));
        QCOMPARE(values.value(QStringLiteral("inactiveColor")).value<QColor>(), QColor(QStringLiteral("#6612ab56")));
        QCOMPARE(values.value(QStringLiteral("glowColor")).value<QColor>(), QColor(QStringLiteral("#8012ab56")));
    }

    void windowIdentityKeepsExplicitAlphaIncludingTransparent()
    {
        QVariantMap values{{QStringLiteral("activeColor"), QColor(QStringLiteral("#00112233"))},
                           {QStringLiteral("inactiveColor"), QStringLiteral("#29112233")},
                           {QStringLiteral("glowColor"), QColor(QStringLiteral("#e8112233"))}};
        resolveThemeParamColors(identityPack(), values, theme());
        QCOMPARE(values.value(QStringLiteral("activeColor")).value<QColor>(), QColor(QStringLiteral("#0012ab56")));
        QCOMPARE(values.value(QStringLiteral("inactiveColor")).value<QColor>(), QColor(QStringLiteral("#2912ab56")));
        QCOMPARE(values.value(QStringLiteral("glowColor")).value<QColor>(), QColor(QStringLiteral("#e812ab56")));
    }

    void userCanDisableIdentityWithoutLosingExplicitColors()
    {
        QVariantMap values{{QStringLiteral("useWindowAccent"), false},
                           {QStringLiteral("activeColor"), QStringLiteral("#b3112233")},
                           {QStringLiteral("inactiveColor"), QStringLiteral("#66112233")},
                           {QStringLiteral("glowColor"), QStringLiteral("#80112233")}};
        const auto before = values;
        resolveThemeParamColors(identityPack(), values, theme());
        QCOMPARE(values, before);
    }

    void identityRequiresDeclaredFlagAndAvailableWindow()
    {
        auto effect = identityPack();
        effect.parameters.removeFirst();
        QVariantMap values{{QStringLiteral("useWindowAccent"), true},
                           {QStringLiteral("activeColor"), QStringLiteral("#b3112233")}};
        const auto before = values;
        resolveThemeParamColors(effect, values, theme());
        QCOMPARE(values, before);

        auto noWindow = theme();
        noWindow.windowAccent = QColor();
        QVERIFY(!noWindow.windowAccent.isValid());
        resolveThemeParamColors(identityPack(), values, noWindow);
        QCOMPARE(values, before);
    }

    void identityDoesNotInjectUndeclaredColors()
    {
        auto effect = identityPack();
        effect.parameters = {effect.parameters.first()};
        QVariantMap values;
        resolveThemeParamColors(effect, values, theme());
        QVERIFY(values.isEmpty());
    }

    void identityPrecedesOtherColorSources()
    {
        auto effect = identityPack();
        for (const auto& flag :
             {QStringLiteral("useThemeNeutral"), QStringLiteral("useSystemAccent"), QStringLiteral("useThemeTint")}) {
            effect.parameters.append(parameter(flag, QStringLiteral("bool"), true));
        }
        QVariantMap values;
        resolveThemeParamColors(effect, values, theme());
        QCOMPARE(values.value(QStringLiteral("activeColor")).value<QColor>(), QColor(QStringLiteral("#b312ab56")));
        QCOMPARE(values.value(QStringLiteral("glowColor")).value<QColor>(), QColor(QStringLiteral("#8012ab56")));

        // Switching identity off restores the existing neutral and theme-tint
        // modes, including the halo alpha rather than an opaque replacement.
        values = {{QStringLiteral("useWindowAccent"), false}, {QStringLiteral("frameContrast"), 0.0}};
        effect.parameters.append(parameter(QStringLiteral("frameContrast"), QStringLiteral("float"), 0.2));
        resolveThemeParamColors(effect, values, theme());
        QCOMPARE(values.value(QStringLiteral("activeColor")).value<QColor>(), theme().background);
        QCOMPARE(values.value(QStringLiteral("glowColor")).value<QColor>(), QColor(QStringLiteral("#80111111")));
    }

    void missingWindowUsesSystemAccentWhenRequested()
    {
        auto effect = identityPack();
        effect.parameters.append(parameter(QStringLiteral("useSystemAccent"), QStringLiteral("bool"), true));
        auto noWindow = theme();
        noWindow.windowAccent = QColor();
        QVERIFY(!noWindow.windowAccent.isValid());
        QVariantMap values;
        resolveThemeParamColors(effect, values, noWindow);
        QCOMPARE(values.value(QStringLiteral("activeColor")).value<QColor>(), noWindow.accent);
        QCOMPARE(values.value(QStringLiteral("inactiveColor")).value<QColor>(), noWindow.inactive);
        QVERIFY(!values.contains(QStringLiteral("glowColor")));
    }

    void maximizedCornersAreOptInAndRestoreAuthoredRadius()
    {
        SurfaceShaderEffect effect;
        effect.parameters = {
            parameter(QStringLiteral("cornerRadius"), QStringLiteral("int"), 18),
            parameter(QStringLiteral("squareWhenMaximized"), QStringLiteral("bool"), false),
        };
        QVariantMap authored{{QStringLiteral("cornerRadius"), 27}};
        auto resolved = authored;
        QVERIFY(resolveWindowStateParams(effect, resolved, true));
        QCOMPARE(resolved, authored);

        authored.insert(QStringLiteral("squareWhenMaximized"), true);
        resolved = authored;
        QVERIFY(resolveWindowStateParams(effect, resolved, true));
        QCOMPARE(resolved.value(QStringLiteral("cornerRadius")).toInt(), 0);
        QCOMPARE(authored.value(QStringLiteral("cornerRadius")).toInt(), 27);

        resolved = authored;
        QVERIFY(resolveWindowStateParams(effect, resolved, false));
        QCOMPARE(resolved.value(QStringLiteral("cornerRadius")).toInt(), 27);

        // A parameter map alone cannot impose undeclared host behavior on a
        // third-party pack that happens to call one of its parameters radius.
        effect.parameters.removeLast();
        resolved = authored;
        QVERIFY(resolveWindowStateParams(effect, resolved, true));
        QCOMPARE(resolved, authored);
    }

    void maximizedHaloHidingHonorsFlagAndUserOverride()
    {
        SurfaceShaderEffect effect;
        effect.parameters = {
            parameter(QStringLiteral("hideWhenMaximized"), QStringLiteral("bool"), true),
            parameter(QStringLiteral("shadowSize"), QStringLiteral("int"), 36),
        };
        QVariantMap values{{QStringLiteral("shadowSize"), 48}};
        const auto authored = values;
        QVERIFY(!resolveWindowStateParams(effect, values, true));
        QCOMPARE(values, authored);
        QVERIFY(resolveWindowStateParams(effect, values, false));
        QCOMPARE(values, authored);

        values.insert(QStringLiteral("hideWhenMaximized"), false);
        QVERIFY(resolveWindowStateParams(effect, values, true));
        QCOMPARE(values.value(QStringLiteral("shadowSize")).toInt(), 48);

        values.insert(QStringLiteral("hideWhenMaximized"), true);
        effect.parameters.removeFirst();
        QVERIFY(resolveWindowStateParams(effect, values, true));
    }
};

QTEST_GUILESS_MAIN(TestSurfaceThemeResolve)
#include "test_surfacethemeresolve.moc"
