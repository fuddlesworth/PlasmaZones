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

#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
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
    /// Numeric keys that legitimately carry no unit: bare counts, indices and
    /// ratios where a suffix would read as noise rather than as information.
    /// Anything NOT listed here has to have a descriptor, so a new numeric key
    /// with a real unit cannot ship rendering as a bare number.
    static const QSet<QString>& unitlessNumericKeys()
    {
        static const QSet<QString> kSet{
            // Bare counts: "3" reads correctly, "3 columns" would be the view's
            // job to phrase, not the unit table's.
            QStringLiteral("Snapping.ZoneSelector/GridColumns"),
            QStringLiteral("Snapping.ZoneSelector/MaxRows"),
            QStringLiteral("Tiling.Algorithm/MasterCount"),
            QStringLiteral("Tiling.Algorithm/MaxWindows"),
            QStringLiteral("Shaders.Audio/Bars"),
            // Font weights are a CSS scale (400 regular, 700 bold), not a
            // quantity of anything.
            QStringLiteral("Snapping.Zones.Labels/FontWeight"),
            QStringLiteral("Scrolling.TabIndicator/FontWeight"),
            // A unitless 0-1 strength scalar. Not a percentage: it is not
            // stored as one and the UI does not present it as one.
            QStringLiteral("Shaders.Audio/NoiseReduction"),
        };
        return kSet;
    }

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
        // Accumulate instead of asserting per row: a mid-loop failure would
        // abort the walk and mask every later drifted choice.
        QStringList missing;
        int checked = 0;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                for (const PhosphorConfig::ChoiceDef& choice : def.choices) {
                    if (SettingsValueLabels::enumLabel(git.key(), def.key, choice.token).isEmpty()) {
                        missing.append(QStringLiteral("%1/%2 token \"%3\"").arg(git.key(), def.key, choice.token));
                    }
                    ++checked;
                }
            }
        }
        missing.sort();
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("no label for: %1").arg(missing.join(QLatin1String(", ")))));
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

        const QSet<QString> labelled = labelledTriples();
        const QSet<QString> orphans = labelled - declared;
        QVERIFY2(orphans.isEmpty(),
                 qPrintable(QStringLiteral("labels with no matching schema choice: %1")
                                .arg(QStringList(orphans.constBegin(), orphans.constEnd()).join(QLatin1String(", ")))));
        // Guard the guard: an empty label table subtracts to an empty orphan
        // set and would pass while checking nothing.
        QVERIFY2(!labelled.isEmpty(), "no labels found — the walk is broken, not clean");
    }

    /// Every numeric key either carries a descriptor or is declared unitless.
    ///
    /// The two slots above walk enum CHOICES, and the orphan slot walks
    /// descriptors -> schema, which catches a stale row. Nothing walked
    /// schema -> descriptors for numeric keys, so a new Int/Double key with a
    /// real unit could ship with no descriptor and stay green: the profile diff
    /// then renders it as a bare number. That is exactly how the four trigger
    /// release graces shipped unitless. This is the missing direction.
    void everyNumericKeyIsDescribedOrDeclaredUnitless()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        QStringList undescribed;
        int checked = 0;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                if (def.expectedType != QMetaType::Int && def.expectedType != QMetaType::Double) {
                    continue;
                }
                if (!def.choices.isEmpty()) {
                    continue; // An enum stored as an int: the choice slots own it.
                }
                const QString qualified = git.key() + QLatin1Char('/') + def.key;
                if (unitlessNumericKeys().contains(qualified)) {
                    continue;
                }
                ++checked;
                const ValueDescriptor d = SettingsValueLabels::descriptorFor(git.key(), def.key);
                if (d.kind == ValueKind::Plain && d.unit.isEmpty()) {
                    undescribed.append(qualified);
                }
            }
        }
        undescribed.sort();
        QVERIFY2(undescribed.isEmpty(),
                 qPrintable(QStringLiteral("numeric keys with no descriptor and not declared unitless: %1")
                                .arg(undescribed.join(QLatin1String(", ")))));
        QVERIFY2(checked > 0, "no numeric keys found in the schema — the walk is broken, not clean");
    }

    /// A declared value must be one its own key actually accepts. The validator
    /// is the authority on that, so feeding each choice through it catches a
    /// choice list that drifted from the enum it was transcribed from — a
    /// reordered enumerator, or a value dropped from the clamp range.
    void everyDeclaredValueSurvivesItsValidator()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        // Accumulate instead of asserting per row so one drifted choice does
        // not mask the rest of a multi-key regression.
        QStringList problems;
        int checked = 0;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                if (!def.validator) {
                    continue;
                }
                for (const PhosphorConfig::ChoiceDef& choice : def.choices) {
                    const QVariant coerced = def.validator(choice.value);
                    if (coerced != choice.value) {
                        problems.append(
                            QStringLiteral("%1/%2 declares %3 (\"%4\") but its validator coerces it to %5")
                                .arg(git.key(), def.key, choice.value.toString(), choice.token, coerced.toString()));
                    }
                    ++checked;
                }
            }
        }
        problems.sort();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1String("\n  "))));
        QVERIFY2(checked > 0, "no validated choices found — the walk is broken, not clean");
    }

    /// A key carrying choices reports itself as an enum, which is what makes
    /// the view resolve it through the token table rather than printing it raw.
    void keysWithChoicesDescribeAsEnum()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        // Accumulate instead of asserting per row so one drifted key does not
        // mask the rest.
        QStringList problems;
        int checked = 0;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                if (def.choices.isEmpty()) {
                    continue;
                }
                const ValueDescriptor descriptor = SettingsValueLabels::descriptorFor(git.key(), def.key);
                if (descriptor.kind != ValueKind::Enum) {
                    problems.append(
                        QStringLiteral("%1/%2 carries choices but does not describe as Enum").arg(git.key(), def.key));
                }
                ++checked;
            }
        }
        problems.sort();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1String("\n  "))));
        QVERIFY2(checked > 0, "no choice-bearing keys found — the walk is broken, not clean");
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
    ///
    /// The check reads the DEFAULT, not the key's whole legal band. One
    /// percent-rendered key is a multiplier rather than a ratio:
    /// Decorations.Performance/BlurScaleMultiplier legally stores up to 2.0
    /// (rendered "200%", which is correct for it) and clears this guard only
    /// because its default is 1.0. If its default ever moves above 1.0, the
    /// right fix is an explicit exemption for that key here, not loosening the
    /// bound for everyone.
    void percentScaledKeysStoreARatio()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        // Accumulate instead of asserting per row so one bad key does not mask
        // the rest.
        QStringList problems;
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
            if (!def) {
                problems.append(
                    QStringLiteral("%1/%2 is scaled for display but absent from the schema").arg(group, key));
                continue;
            }
            const double value = def->defaultValue.toDouble();
            if (value < 0.0 || value > 1.0) {
                problems.append(QStringLiteral("%1/%2 is scaled by %3 for display but defaults to %4, "
                                               "which is not a 0.0-1.0 ratio")
                                    .arg(group, key)
                                    .arg(descriptor.displayScale)
                                    .arg(value));
            }
            ++checked;
        }
        problems.sort();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1String("\n  "))));
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

    /// Every (group, key) pair a QML picker passes to valueOptions() must name
    /// a key that declares choices.
    ///
    /// QML has no compile-time checking, so a mistyped pair here fails at
    /// runtime as an empty combo box — on a page nobody may open for weeks.
    /// Scanning the sources turns that into a test failure. This is the only
    /// guard covering the QML side of the table, since nothing else can see
    /// those string literals.
    void everyQmlValueOptionsCallResolves()
    {
        const QDir qmlDir(QStringLiteral(P_SOURCE_DIR) + QStringLiteral("/src/settings/qml"));
        QVERIFY2(qmlDir.exists(), qPrintable(QStringLiteral("no QML directory at %1").arg(qmlDir.path())));

        const PhosphorConfig::Schema schema = buildSettingsSchema();
        // valueOptions("Group.Path", "Key") — tolerant of whitespace, and of
        // either quote style QML allows.
        // Escaped rather than a raw string literal: moc does not parse R"(...)"
        // and would read the first embedded quote as the end of the string,
        // swallowing the rest of the file and leaving this class without a
        // vtable.
        static const QRegularExpression call(
            QStringLiteral("valueOptions\\s*\\(\\s*[\"']([^\"']+)[\"']\\s*,\\s*[\"']([^\"']+)[\"']\\s*\\)"));

        QStringList problems;
        int calls = 0;
        // Recursive on purpose: nearly every page lives under qml/pages/**, so
        // a flat entryInfoList walk saw only the handful of top-level files and
        // silently guarded 3 of the ~23 real call sites while reading green.
        QDirIterator files(qmlDir.path(), {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QFileInfo info(files.next());
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            const QString text = QString::fromUtf8(file.readAll());
            auto it = call.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                const QString group = match.captured(1);
                const QString key = match.captured(2);
                ++calls;
                const PhosphorConfig::KeyDef* def = schema.findKey(group, key);
                if (!def) {
                    problems.append(QStringLiteral("%1: %2/%3 is not in the schema").arg(info.fileName(), group, key));
                } else if (def->choices.isEmpty()) {
                    problems.append(QStringLiteral("%1: %2/%3 declares no choices, so the picker would be empty")
                                        .arg(info.fileName(), group, key));
                }
            }
        }

        problems.sort();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1String("\n  "))));
        // A floor a single-file scan cannot satisfy: the settings QML carries
        // over twenty valueOptions() calls, so a count this low means the walk
        // regressed to a subset of the tree, not that pickers were removed.
        QVERIFY2(calls > 10,
                 qPrintable(QStringLiteral("only %1 valueOptions() calls found in QML — the scan is broken, not clean")
                                .arg(calls)));
    }

    /// Every UI-subset token must name a choice its key actually declares —
    /// a subset that drifts from the schema would silently empty the picker.
    void uiSubsetTokensAreDeclaredChoices()
    {
        const PhosphorConfig::Schema& schema = cachedSettingsSchema();
        // Accumulate instead of asserting per row so one drifted subset does
        // not mask the rest.
        QStringList problems;
        int checked = 0;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                const QStringList subset = SettingsValueLabels::uiChoiceSubset(git.key(), def.key);
                if (subset.isEmpty()) {
                    continue;
                }
                QSet<QString> declared;
                for (const PhosphorConfig::ChoiceDef& choice : def.choices) {
                    declared.insert(choice.token);
                }
                for (const QString& token : subset) {
                    if (!declared.contains(token)) {
                        problems.append(QStringLiteral("%1/%2 subset token \"%3\" is not a declared choice")
                                            .arg(git.key(), def.key, token));
                    }
                }
                // A subset must actually narrow; equal-or-wider is a mistake.
                if (subset.size() >= def.choices.size()) {
                    problems.append(
                        QStringLiteral("%1/%2 subset does not narrow the choice set").arg(git.key(), def.key));
                }
                ++checked;
            }
        }
        problems.sort();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1String("\n  "))));
        QVERIFY2(checked > 0, "no UI subsets found — the walk is broken, not clean");
    }

    /// The audio input picker offers exactly the supported three backends,
    /// while all ten stay legal on disk. This is the contract the picker
    /// migration briefly broke by offering every legal token.
    void audioInputPickerOffersSupportedSubsetOnly()
    {
        using CD = ConfigDefaults;
        const QStringList subset = SettingsValueLabels::uiChoiceSubset(CD::shadersAudioGroup(), CD::inputMethodKey());
        QCOMPARE(subset, (QStringList{QStringLiteral("auto"), QStringLiteral("pipewire"), QStringLiteral("pulse")}));
        QCOMPARE(cachedSettingsSchema().choicesFor(CD::shadersAudioGroup(), CD::inputMethodKey()).size(), 10);
    }

    /// WindowAppearancePage sources ONE valueOptions("Windows","BorderScope")
    /// list for all three "Apply to" pickers (border, title bar, opacity/tint),
    /// so the three scope keys must declare the identical token set in the
    /// identical order. Diverging one key would render its picker blank with
    /// nothing else catching it.
    void windowScopeKeysShareOneTokenSet()
    {
        using CD = ConfigDefaults;
        const PhosphorConfig::Schema& schema = cachedSettingsSchema();
        const auto tokensOf = [&](const QString& key) {
            QStringList tokens;
            for (const PhosphorConfig::ChoiceDef& choice : schema.choicesFor(CD::windowsAppearanceGroup(), key)) {
                tokens.append(choice.token);
            }
            return tokens;
        };
        const QStringList border = tokensOf(CD::borderScopeKey());
        QVERIFY(!border.isEmpty());
        QCOMPARE(tokensOf(CD::titleBarScopeKey()), border);
        QCOMPARE(tokensOf(CD::opacityTintScopeKey()), border);
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
