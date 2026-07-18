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
