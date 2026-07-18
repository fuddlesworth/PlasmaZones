// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settingsvaluelabels.cpp
 * @brief Binds the config schema's declared choices to their user-facing words.
 *
 * A key's legal values live in the schema (PhosphorConfig::ChoiceDef, tokens
 * only) and the words for them live app-side, keyed by (group, key, token).
 * Split like that, the two halves can drift silently: a new choice ships with
 * no label and renders as a bare token, or a label outlives the choice it named
 * and is dead weight nobody notices.
 *
 * These tests are the seam that makes drift a build failure instead:
 *   - every declared choice has a label,
 *   - every label names a declared choice,
 *   - every declared value survives its own key's validator, which catches a
 *     choice list that fell out of step with the enum it mirrors.
 */

#include <QSet>
#include <QTest>

#include <PhosphorConfig/Schema.h>

#include "config/settingsschema.h"
#include "config/configdefaults.h"
#include "config/settingsvaluelabels.h"

using namespace PlasmaZones;

class TestSettingsValueLabels : public QObject
{
    Q_OBJECT

private:
    /// (group, key, token) triples that carry a label, as one joined string per
    /// entry so the two directions can be compared as sets.
    static QSet<QString> labelledTriples()
    {
        QSet<QString> out;
        const QVariantList rows = SettingsValueLabels::allEnumLabels();
        for (const QVariant& row : rows) {
            const QVariantMap map = row.toMap();
            out.insert(map.value(QStringLiteral("group")).toString() + QLatin1Char('/')
                       + map.value(QStringLiteral("key")).toString() + QLatin1Char('/')
                       + map.value(QStringLiteral("token")).toString());
        }
        return out;
    }

private Q_SLOTS:
    /// Every choice the schema declares can be named. Without this, adding a
    /// value to an enum ships a diff row reading "layoutPreview".
    void everyDeclaredChoiceHasALabel()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        int checked = 0;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                for (const PhosphorConfig::ChoiceDef& choice : def.choices) {
                    const QString label = SettingsValueLabels::enumLabel(git.key(), def.key, choice.token);
                    QVERIFY2(
                        !label.isEmpty(),
                        qPrintable(
                            QStringLiteral("no label for %1/%2 token \"%3\"").arg(git.key(), def.key, choice.token)));
                    ++checked;
                }
            }
        }
        // Guard the guard: an empty schema walk would pass every assertion
        // above while checking nothing at all.
        QVERIFY2(checked > 0, "no choices found in the schema — the walk is broken, not clean");
    }

    /// And the reverse: a label whose choice no longer exists is stale, and
    /// would otherwise sit in the translation catalogue forever.
    void everyLabelNamesADeclaredChoice()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        QSet<QString> declared;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                for (const PhosphorConfig::ChoiceDef& choice : def.choices) {
                    declared.insert(git.key() + QLatin1Char('/') + def.key + QLatin1Char('/') + choice.token);
                }
            }
        }

        const QSet<QString> orphans = labelledTriples() - declared;
        QVERIFY2(orphans.isEmpty(),
                 qPrintable(QStringLiteral("labels with no matching schema choice: %1")
                                .arg(QStringList(orphans.constBegin(), orphans.constEnd()).join(QLatin1String(", ")))));
    }

    /// A declared value must be one its own key actually accepts. The validator
    /// is the authority on that, so feeding each choice through it catches a
    /// choice list that drifted from the enum it was transcribed from — a
    /// reordered enumerator, or a value dropped from the clamp range.
    void everyDeclaredValueSurvivesItsValidator()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                if (!def.validator) {
                    continue;
                }
                for (const PhosphorConfig::ChoiceDef& choice : def.choices) {
                    const QVariant coerced = def.validator(choice.value);
                    QVERIFY2(coerced == choice.value,
                             qPrintable(QStringLiteral("%1/%2 declares %3 (\"%4\") but its validator coerces it to %5")
                                            .arg(git.key(), def.key, choice.value.toString(), choice.token,
                                                 coerced.toString())));
                }
            }
        }
    }

    /// A key carrying choices reports itself as an enum, which is what makes
    /// the view resolve it through the token table rather than printing it raw.
    void keysWithChoicesDescribeAsEnum()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                if (def.choices.isEmpty()) {
                    continue;
                }
                const ValueDescriptor descriptor = SettingsValueLabels::descriptorFor(git.key(), def.key);
                QCOMPARE(descriptor.kind, ValueKind::Enum);
            }
        }
    }

    /// An undescribed key degrades to Plain rather than failing, matching the
    /// rule views' discipline of always rendering something.
    void unknownKeyFallsBackToPlain()
    {
        const ValueDescriptor descriptor =
            SettingsValueLabels::descriptorFor(QStringLiteral("No.Such.Group"), QStringLiteral("NoSuchKey"));
        QCOMPARE(descriptor.kind, ValueKind::Plain);
        QVERIFY(descriptor.unit.isEmpty());
        QCOMPARE(descriptor.displayScale, 1.0);
    }

    /// Every described key must be one the schema actually declares. The table
    /// is hand-written from an audit of the settings pages, so a mistyped or
    /// renamed (group, key) pair is the most likely error in it — and the
    /// failure mode is silent, since a pair that matches nothing simply never
    /// resolves and the value renders raw forever.
    void everyDescribedKeyExistsInTheSchema()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        QStringList missing;
        const QVariantList described = SettingsValueLabels::allDescribedKeys();
        for (const QVariant& row : described) {
            const QVariantMap map = row.toMap();
            const QString group = map.value(QStringLiteral("group")).toString();
            const QString key = map.value(QStringLiteral("key")).toString();
            if (!schema.findKey(group, key)) {
                missing.append(group + QLatin1Char('/') + key);
            }
        }
        missing.sort();
        QVERIFY2(
            missing.isEmpty(),
            qPrintable(
                QStringLiteral("described keys absent from the schema: %1").arg(missing.join(QLatin1String(", ")))));
        QVERIFY2(!described.isEmpty(), "no described keys — the walk is broken, not clean");
    }

    /// A key displayed as a percentage must actually persist a 0.0-1.0 ratio.
    /// Scaling a key that already stores 0-100 would render "8000%", and the
    /// only signal that the scale is wrong is the number a user sees.
    void percentScaledKeysStoreARatio()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        int checked = 0;
        const QVariantList described = SettingsValueLabels::allDescribedKeys();
        for (const QVariant& row : described) {
            const QVariantMap map = row.toMap();
            const QString group = map.value(QStringLiteral("group")).toString();
            const QString key = map.value(QStringLiteral("key")).toString();
            const ValueDescriptor descriptor = SettingsValueLabels::descriptorFor(group, key);
            if (descriptor.displayScale == 1.0) {
                continue;
            }
            const PhosphorConfig::KeyDef* def = schema.findKey(group, key);
            QVERIFY(def);
            const double value = def->defaultValue.toDouble();
            QVERIFY2(value >= 0.0 && value <= 1.0,
                     qPrintable(QStringLiteral("%1/%2 is scaled by %3 for display but defaults to %4, "
                                               "which is not a 0.0-1.0 ratio")
                                    .arg(group, key)
                                    .arg(descriptor.displayScale)
                                    .arg(value)));
            ++checked;
        }
        QVERIFY2(checked > 0, "no scaled keys found — the walk is broken, not clean");
    }

    /// The end-to-end resolution a diff row depends on: stored value → schema
    /// choice token → translated word.
    void enumValueResolvesToItsLabel()
    {
        using CD = ConfigDefaults;
        QCOMPARE(SettingsValueLabels::displayText(CD::snappingZoneSelectorGroup(), CD::layoutModeKey(), 2),
                 QStringLiteral("Vertical"));
        QCOMPARE(SettingsValueLabels::displayText(CD::snappingZoneSelectorGroup(), CD::layoutModeKey(), 0),
                 QStringLiteral("Grid"));
        // A token-valued key resolves the same way.
        QCOMPARE(SettingsValueLabels::displayText(CD::renderingGroup(), CD::backendKey(), QStringLiteral("opengl")),
                 QStringLiteral("OpenGL"));
    }

    /// A value outside the declared set yields nothing, so the view shows the
    /// raw value rather than inventing a label for it.
    void undeclaredEnumValueResolvesToNothing()
    {
        using CD = ConfigDefaults;
        QVERIFY(SettingsValueLabels::displayText(CD::snappingZoneSelectorGroup(), CD::layoutModeKey(), 99).isEmpty());
    }

    /// Numbers carry their unit, and a ratio is scaled into the percentage the
    /// settings page shows rather than reported as "0.8".
    void numbersCarryUnitAndScale()
    {
        using CD = ConfigDefaults;
        QCOMPARE(SettingsValueLabels::displayText(CD::windowsAppearanceGroup(), CD::widthKey(), 2),
                 QStringLiteral("2 px"));
        QCOMPARE(SettingsValueLabels::displayText(CD::snappingZonesOpacityGroup(), CD::activeKey(), 0.8),
                 QStringLiteral("80%"));
        // A ratio that lands between whole percents keeps a decimal instead of
        // rounding to a number that never round-trips.
        QCOMPARE(SettingsValueLabels::displayText(CD::snappingZonesOpacityGroup(), CD::activeKey(), 0.335),
                 QStringLiteral("33.5%"));
    }

    /// Where the UI presents 0 as "Off", the diff says so too.
    void zeroReadsAsOffWhereTheUiSaysSo()
    {
        using CD = ConfigDefaults;
        QCOMPARE(SettingsValueLabels::displayText(CD::exclusionsGroup(), CD::minimumWindowWidthKey(), 0),
                 QStringLiteral("Off"));
        QCOMPARE(SettingsValueLabels::displayText(CD::exclusionsGroup(), CD::minimumWindowWidthKey(), 120),
                 QStringLiteral("120 px"));
    }

    /// Live-resolved kinds return nothing from this side on purpose — only the
    /// view knows which monitors are connected or which packs are installed.
    void liveKindsDeferToTheView()
    {
        using CD = ConfigDefaults;
        QVERIFY(SettingsValueLabels::displayText(CD::animationsGroup(), CD::shaderProfileTreeKey(),
                                                 QStringLiteral("phosphor-stream"))
                    .isEmpty());
        QCOMPARE(SettingsValueLabels::kindName(
                     SettingsValueLabels::descriptorFor(CD::animationsGroup(), CD::shaderProfileTreeKey()).kind),
                 QStringLiteral("shaderPack"));
    }

    /// The same token under two keys means two different things, which is the
    /// reason the table is keyed by (group, key) and not by token alone.
    void tokenMeaningIsKeyLocal()
    {
        using CD = ConfigDefaults;
        const QString drag =
            SettingsValueLabels::enumLabel(CD::tilingBehaviorGroup(), CD::dragBehaviorKey(), QStringLiteral("float"));
        const QString overflow = SettingsValueLabels::enumLabel(CD::tilingBehaviorGroup(), CD::overflowBehaviorKey(),
                                                                QStringLiteral("float"));
        QVERIFY(!drag.isEmpty());
        QVERIFY(!overflow.isEmpty());
        QVERIFY2(drag != overflow, "\"float\" must not collapse to one label across two keys");
    }
};

QTEST_MAIN(TestSettingsValueLabels)
#include "test_settingsvaluelabels.moc"
