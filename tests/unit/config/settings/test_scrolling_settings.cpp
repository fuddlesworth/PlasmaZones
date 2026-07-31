// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_settings.cpp
 * @brief Schema-level guards for the Scrolling group and the
 *        shortcut-default invariants the scrolling family made load-bearing.
 *
 * Two defect classes shipped once and are pinned here so they cannot recur:
 *   - duplicate default keybindings (KGlobalAccel grants a chord to ONE
 *     action; the loser silently never fires), and
 *   - Shift+symbol default spellings, which can never fire on Wayland
 *     because KWin consumes Shift in the keysym translation (see the
 *     toggleCheatsheetShortcut rationale in configdefaults.h).
 * Plus the scrolling enum keys' fall-back-to-default validator behaviour
 * (validIntOr, NOT clamp — an out-of-range stored value must not silently
 * become the nearest enumerator) and the preset-list numeric canonicalizer.
 */

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorConfig/Schema.h>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "config/settingsschema.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;

class TestScrollingSettings : public QObject
{
    Q_OBJECT

private:
    static const PhosphorConfig::KeyDef* findKey(const PhosphorConfig::Schema& schema, const QString& group,
                                                 const QString& key)
    {
        const auto it = schema.groups.constFind(group);
        if (it == schema.groups.constEnd()) {
            return nullptr;
        }
        for (const PhosphorConfig::KeyDef& def : *it) {
            if (def.key == key) {
                return &def;
            }
        }
        return nullptr;
    }

private Q_SLOTS:
    /// No two non-empty shortcut DEFAULTS may collide anywhere in the
    /// schema's Shortcuts.* groups. Data-driven over the schema (not a
    /// hand-maintained accessor list) so future shortcut families are
    /// covered automatically.
    void noDuplicateShortcutDefaults()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        QHash<QString, QString> chordToKey;
        QStringList collisions;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            if (!git.key().startsWith(QLatin1String("Shortcuts"))) {
                continue;
            }
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                const QString raw = def.defaultValue.toString();
                if (raw.isEmpty()) {
                    continue; // deliberately unbound defaults are fine
                }
                // Canonicalise through QKeySequence so spelling variants of
                // the same chord ("Meta+Alt+=" vs "Alt+Meta+=", stray
                // whitespace) still collide; an unparsable spelling falls
                // back to the raw string rather than silently passing.
                const QString parsed =
                    QKeySequence::fromString(raw, QKeySequence::PortableText).toString(QKeySequence::PortableText);
                const QString chord = parsed.isEmpty() ? raw : parsed;
                const QString qualified = git.key() + QLatin1Char('/') + def.key;
                if (chordToKey.contains(chord)) {
                    collisions.append(chord + QLatin1String(": ") + chordToKey.value(chord) + QLatin1String(" vs ")
                                      + qualified);
                } else {
                    chordToKey.insert(chord, qualified);
                }
            }
        }
        QVERIFY2(collisions.isEmpty(), qPrintable(collisions.join(QLatin1String("; "))));
    }

    /// No default may spell a Shift+symbol chord — those never fire on
    /// Wayland (KWin strips Shift as a consumed modifier when the keysym
    /// translation uses it). Shift+letter and Shift+named-key are fine.
    void noShiftSymbolDefaults()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        // Structural check through QKeySequence, not a text regex: it sees
        // every combination of a multi-chord sequence and tolerates
        // whitespace and modifier-order variants. A combination offends
        // when Shift is held and the base key is a printable
        // non-alphanumeric (letters, digits, and navigation keys like
        // Home/PgUp are fine). Known over-reach: Shift+Space would be
        // flagged although its keysym does not shift — acceptable, no
        // default uses it and a false positive here fails loudly. A symbol spelled as a NAMED key ("Plus")
        // decodes to Key_unknown and is caught by allDefaultsParseCleanly
        // below, not by this guard.
        QStringList offenders;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            if (!git.key().startsWith(QLatin1String("Shortcuts"))) {
                continue;
            }
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                const QString chord = def.defaultValue.toString();
                const QKeySequence seq = QKeySequence::fromString(chord, QKeySequence::PortableText);
                bool offends = false;
                for (int i = 0; i < seq.count(); ++i) {
                    const QKeyCombination kc = seq[i];
                    const int key = kc.key();
                    if ((kc.keyboardModifiers() & Qt::ShiftModifier) && key > 0 && key < 0x100
                        && !QChar(key).isLetterOrNumber()) {
                        offends = true;
                        break;
                    }
                }
                if (offends) {
                    offenders.append(git.key() + QLatin1Char('/') + def.key + QLatin1String(" = ") + chord);
                }
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1String("; "))));
    }

    /// Every non-empty shortcut default must parse to a real key sequence
    /// with no Key_unknown combination — an unparsable spelling is a DEAD
    /// shortcut (KGlobalAccel cannot bind it), which is exactly the defect
    /// class this file exists to prevent, and both guards above silently
    /// tolerate it (the duplicate check falls back to the raw string; the
    /// Shift guard's key < 0x100 filter excludes Key_unknown).
    void allDefaultsParseCleanly()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        QStringList offenders;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            if (!git.key().startsWith(QLatin1String("Shortcuts"))) {
                continue;
            }
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                const QString chord = def.defaultValue.toString();
                if (chord.isEmpty()) {
                    continue;
                }
                const QKeySequence seq = QKeySequence::fromString(chord, QKeySequence::PortableText);
                bool bad = seq.count() == 0;
                for (int i = 0; i < seq.count() && !bad; ++i) {
                    bad = seq[i].key() == Qt::Key_unknown;
                }
                if (bad) {
                    offenders.append(git.key() + QLatin1Char('/') + def.key + QLatin1String(" = ") + chord);
                }
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1String("; "))));
    }

    /// The scrolling enums fall back to their DEFAULT on out-of-range input
    /// (validIntOr), never to the nearest enumerator, matching the engine's
    /// own snap-to-default guard.
    void scrollingEnumsFallBackToDefault()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::scrollingGroup();

        const auto* center = findKey(schema, group, ConfigDefaults::centerFocusedColumnKey());
        QVERIFY(center && center->validator);
        QCOMPARE(center->validator(99).toInt(), ConfigDefaults::scrollingCenterFocusedColumn());
        QCOMPARE(center->validator(2).toInt(), 2); // legal value passes through

        const auto* kind = findKey(schema, group, ConfigDefaults::defaultColumnWidthKindKey());
        QVERIFY(kind && kind->validator);
        QCOMPARE(kind->validator(-1).toInt(), ConfigDefaults::scrollingDefaultColumnWidthKind());
        QCOMPARE(kind->validator(2).toInt(), 2);
        // Preset (3) is a legal kind; 4 is not (append-only vocabulary).
        QCOMPARE(kind->validator(ConfigDefaults::scrollingWidthKindPreset()).toInt(),
                 ConfigDefaults::scrollingWidthKindPreset());
        QCOMPARE(kind->validator(4).toInt(), ConfigDefaults::scrollingDefaultColumnWidthKind());

        const auto* widthPresetIdx = findKey(schema, group, ConfigDefaults::defaultColumnWidthPresetIndexKey());
        QVERIFY(widthPresetIdx && widthPresetIdx->validator);
        QCOMPARE(widthPresetIdx->validator(-1).toInt(), 0);
        QCOMPARE(widthPresetIdx->validator(99).toInt(), ConfigDefaults::scrollingPresetIndexMax());

        const auto* heightKind = findKey(schema, group, ConfigDefaults::defaultWindowHeightKindKey());
        QVERIFY(heightKind && heightKind->validator);
        QCOMPARE(heightKind->validator(9).toInt(), ConfigDefaults::scrollingDefaultWindowHeightKind());
        QCOMPARE(heightKind->validator(ConfigDefaults::scrollingHeightKindPreset()).toInt(),
                 ConfigDefaults::scrollingHeightKindPreset());

        const auto* heightValue = findKey(schema, group, ConfigDefaults::defaultWindowHeightValueKey());
        QVERIFY(heightValue && heightValue->validator);
        QCOMPARE(heightValue->validator(1.0).toDouble(), ConfigDefaults::scrollingDefaultWindowHeightMin());
        QCOMPARE(heightValue->validator(99999.0).toDouble(), ConfigDefaults::scrollingDefaultWindowHeightMax());

        const auto* insertPos =
            findKey(schema, ConfigDefaults::scrollingBehaviorGroup(), ConfigDefaults::insertPositionKey());
        QVERIFY(insertPos && insertPos->validator);
        QCOMPARE(insertPos->validator(9).toInt(), ConfigDefaults::scrollingInsertPosition());
        QCOMPARE(insertPos->validator(ConfigDefaults::scrollingInsertIntoActiveColumn()).toInt(),
                 ConfigDefaults::scrollingInsertIntoActiveColumn());

        const auto* display = findKey(schema, group, ConfigDefaults::defaultColumnDisplayKey());
        QVERIFY(display && display->validator);
        QCOMPARE(display->validator(7).toInt(), ConfigDefaults::scrollingDefaultColumnDisplay());
        QCOMPARE(display->validator(1).toInt(), 1);
    }

    /// Scrolling.Behavior schema guards: the sticky enum falls back to its
    /// default (validIntOr, matching the file's enum convention), the two
    /// adjust-step percents clamp into their declared range (clampInt — a
    /// numeric range, not an enum), and every key ships the ConfigDefaults
    /// default.
    void scrollingBehaviorSchemaValidates()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::scrollingBehaviorGroup();

        const auto* sticky = findKey(schema, group, ConfigDefaults::stickyWindowHandlingKey());
        QVERIFY(sticky && sticky->validator);
        QCOMPARE(sticky->validator(99).toInt(), ConfigDefaults::scrollingStickyWindowHandling());
        QCOMPARE(sticky->validator(ConfigDefaults::scrollingStickyIgnoreAll()).toInt(),
                 ConfigDefaults::scrollingStickyIgnoreAll());

        const auto* widthStep = findKey(schema, group, ConfigDefaults::columnWidthStepPercentKey());
        QVERIFY(widthStep && widthStep->validator);
        QCOMPARE(widthStep->validator(0).toInt(), ConfigDefaults::scrollingStepPercentMin());
        QCOMPARE(widthStep->validator(999).toInt(), ConfigDefaults::scrollingStepPercentMax());
        QCOMPARE(widthStep->defaultValue.toInt(), ConfigDefaults::scrollingColumnWidthStepPercent());

        const auto* heightStep = findKey(schema, group, ConfigDefaults::windowHeightStepPercentKey());
        QVERIFY(heightStep && heightStep->validator);
        QCOMPARE(heightStep->validator(-5).toInt(), ConfigDefaults::scrollingStepPercentMin());
        QCOMPARE(heightStep->defaultValue.toInt(), ConfigDefaults::scrollingWindowHeightStepPercent());

        const auto* focusNew = findKey(schema, group, ConfigDefaults::focusNewWindowsKey());
        QVERIFY(focusNew);
        QCOMPARE(focusNew->defaultValue.toBool(), ConfigDefaults::scrollingFocusNewWindows());
        const auto* ffm = findKey(schema, group, ConfigDefaults::focusFollowsMouseKey());
        QVERIFY(ffm);
        QCOMPARE(ffm->defaultValue.toBool(), ConfigDefaults::scrollingFocusFollowsMouse());
        const auto* respectMin = findKey(schema, group, ConfigDefaults::respectMinimumSizeKey());
        QVERIFY(respectMin);
        QCOMPARE(respectMin->defaultValue.toBool(), ConfigDefaults::scrollingRespectMinimumSize());
        const auto* restore = findKey(schema, group, ConfigDefaults::restoreOnLoginKey());
        QVERIFY(restore);
        QCOMPARE(restore->defaultValue.toBool(), ConfigDefaults::scrollingRestoreStripsOnLogin());
    }

    /// Behavior setters follow the standard emit-once contract: an
    /// effective change fires the property NOTIFY plus settingsChanged
    /// exactly once each, and a same-value write is a full no-op.
    void behaviorSettersEmitOnce()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;

        QSignalSpy changedSpy(&settings, &Settings::settingsChanged);
        QSignalSpy stickySpy(&settings, &Settings::scrollingStickyWindowHandlingChanged);

        settings.setScrollingStickyWindowHandling(ConfigDefaults::scrollingStickyIgnoreAll());
        QCOMPARE(stickySpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);
        // Same value again: no emit.
        settings.setScrollingStickyWindowHandling(ConfigDefaults::scrollingStickyIgnoreAll());
        QCOMPARE(stickySpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);
        // Out-of-range write: the schema validator coerces the re-read back
        // to the default, which IS a change from IgnoreAll.
        settings.setScrollingStickyWindowHandling(42);
        QCOMPARE(settings.scrollingStickyWindowHandling(), ConfigDefaults::scrollingStickyWindowHandling());

        QSignalSpy stepSpy(&settings, &Settings::scrollingColumnWidthStepPercentChanged);
        const int preChanged = changedSpy.count();
        settings.setScrollingColumnWidthStepPercent(25);
        QCOMPARE(settings.scrollingColumnWidthStepPercent(), 25);
        QCOMPARE(stepSpy.count(), 1);
        QCOMPARE(changedSpy.count(), preChanged + 1);
        // Clamped write: 999 stores as the max.
        settings.setScrollingColumnWidthStepPercent(999);
        QCOMPARE(settings.scrollingColumnWidthStepPercent(), ConfigDefaults::scrollingStepPercentMax());
    }

    /// Preset lists canonicalize to numeric proportions in (0, 1]: junk and
    /// out-of-range entries are dropped so the stored value always equals
    /// the effective one (the engine silently ignores anything else).
    void presetListsCanonicalizeNumerically()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::scrollingGroup();

        const auto* widths = findKey(schema, group, ConfigDefaults::presetColumnWidthsKey());
        QVERIFY(widths && widths->validator);
        QCOMPARE(widths->validator(QStringLiteral("0.25, abc, 5, 0.5, 0.5, -1")).toString(),
                 QStringLiteral("0.25,0.5"));
        // The shipped default survives its own validator UNCHANGED — a
        // non-empty result is not enough: a validator that silently dropped
        // or reordered entries would still pass an isEmpty check while the
        // stored list stopped matching the shipped one.
        QCOMPARE(widths->validator(widths->defaultValue).toString(), ConfigDefaults::scrollingPresetColumnWidths());
        // The rule is the bare (0, 1] one the engine applies to a preset
        // entry. The scalar width key's kind-aware floor governs that key
        // alone, so a very narrow preset is legal here and must survive
        // rather than being dropped as sub-floor.
        QCOMPARE(widths->validator(QStringLiteral("0.01, 0.5")).toString(), QStringLiteral("0.01,0.5"));

        const auto* heights = findKey(schema, group, ConfigDefaults::presetWindowHeightsKey());
        QVERIFY(heights && heights->validator);
        // Nothing survives (cleared field / all-garbage) → snap to the
        // key's default, never persist an empty accepted-but-dead value
        // while the engine silently cycles its built-ins.
        QCOMPARE(heights->validator(QStringLiteral("")).toString(), ConfigDefaults::scrollingPresetWindowHeights());
        QCOMPARE(heights->validator(QStringLiteral("junk, -3, 2.0")).toString(),
                 ConfigDefaults::scrollingPresetWindowHeights());
        QCOMPARE(heights->validator(heights->defaultValue).toString(), ConfigDefaults::scrollingPresetWindowHeights());
        // The WIDTHS key snaps to ITS default too (a widths-only regression
        // must not hide behind the heights-only assertion above).
        QCOMPARE(widths->validator(QStringLiteral("")).toString(), ConfigDefaults::scrollingPresetColumnWidths());
        // Size cap: a hand-edited file must not smuggle an unbounded list
        // past the setter path — 26 entries canonicalize to at most 16. Every
        // entry is a legal proportion, so the cap is the only thing trimming.
        QStringList many;
        for (int i = 5; i <= 30; ++i) {
            many.append(QString::number(i / 100.0));
        }
        const QStringList capped = widths->validator(many.join(QLatin1Char(','))).toString().split(QLatin1Char(','));
        QCOMPARE(capped.size(), 16);
    }

    /// The width VALUE key clamps into the schema range (backstop; the
    /// kind-aware clamp lives in the Settings setter).
    void widthValueClampsToSchemaRange()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const auto* value =
            findKey(schema, ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
        QVERIFY(value && value->validator);
        QCOMPARE(value->validator(0.001).toDouble(), ConfigDefaults::scrollingDefaultColumnWidthValueMin());
        QCOMPARE(value->validator(99999.0).toDouble(), ConfigDefaults::scrollingDefaultColumnWidthFixedMax());
    }

    /// The kind-aware clamp in the SETTER, which the schema's wider
    /// clampDouble cannot express: under Fixed the value is bounded by the
    /// pixel range, under Proportion by [ValueMin, ProportionMax]. Without
    /// this the two halves of the shared value key are pinned only at their
    /// union, so a proportion-magnitude write under Fixed (or a pixel-
    /// magnitude one under Proportion) would sail through.
    void widthValueClampsPerKind()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;

        settings.setScrollingDefaultColumnWidthKind(ConfigDefaults::scrollingWidthKindFixed());
        // A proportion-magnitude write under Fixed hits the pixel floor.
        settings.setScrollingDefaultColumnWidthValue(0.5);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), ConfigDefaults::scrollingDefaultColumnWidthFixedMin());
        settings.setScrollingDefaultColumnWidthValue(99999.0);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), ConfigDefaults::scrollingDefaultColumnWidthFixedMax());
        // In-range pixels pass through untouched.
        settings.setScrollingDefaultColumnWidthValue(640.0);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), 640.0);

        settings.setScrollingDefaultColumnWidthKind(ConfigDefaults::scrollingWidthKindProportion());
        // A pixel-magnitude write under Proportion hits the 100% ceiling —
        // NOT the fixed ceiling the schema clamp would have allowed.
        settings.setScrollingDefaultColumnWidthValue(5.0);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(),
                 ConfigDefaults::scrollingDefaultColumnWidthProportionMax());
        settings.setScrollingDefaultColumnWidthValue(0.001);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), ConfigDefaults::scrollingDefaultColumnWidthValueMin());
        settings.setScrollingDefaultColumnWidthValue(0.35);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), 0.35);
    }

    /// Full kind-transition table for the shared width value key. The kind
    /// setter owns the coercion (see setScrollingDefaultColumnWidthKind):
    /// each arm re-seeds only when the stored value cannot belong to the kind
    /// being entered — Fixed below the pixel floor, Proportion above 1.0 —
    /// ClientDecides leaves the value untouched, and a same-kind write is a
    /// full no-op. A value of either kind therefore survives a ClientDecides
    /// round trip intact. Each effective flip must emit
    /// settingsChanged exactly ONCE — a double emit runs the engine's
    /// refresh+retile sweep twice per user action.
    void widthKindTransitionsCoerceAndEmitOnce()
    {
        const int kindP = ConfigDefaults::scrollingWidthKindProportion();
        const int kindF = ConfigDefaults::scrollingWidthKindFixed();
        const int kindC = ConfigDefaults::scrollingWidthKindClientDecides();
        const qreal seededPx = ConfigDefaults::scrollingDefaultColumnWidthFixedPx();
        const qreal defaultProp = ConfigDefaults::scrollingDefaultColumnWidthValue();
        // Seeds deliberately DIFFER from the ConfigDefaults values (0.35 vs
        // the 0.5 default, 1200 vs the 800 re-seed): with default-equal
        // seeds every "value untouched" row would also pass under a bogus
        // unconditional re-seed to the default.
        const qreal seedProp = 0.35;
        const qreal seedPx = 1200.0;

        struct Row
        {
            const char* name;
            int seedKind; // the kind the seed VALUE is written under
            qreal seedValue;
            int fromKind; // reached from seedKind via an extra hop if needed
            int toKind;
            qreal expectedValue;
            int expectedChanged; // settingsChanged emits
            int expectedKind; // scrollingDefaultColumnWidthKindChanged emits
            int expectedValueSig; // scrollingDefaultColumnWidthValueChanged emits
        };
        // All nine kind pairs; C→P and C→F appear more than once because
        // their outcome depends on WHAT the ClientDecides hop left stored (it
        // deliberately leaves the value key untouched, so both a proportion
        // and a pixel count can be sitting there). "C->F 1200px retained" is
        // the pin for the Fixed arm's preservation test: a seed that differs
        // from the 800 re-seed, so an unconditional re-seed on entry to Fixed
        // fails the row. The 800px row keeps the case where the coercion is a
        // no-op regardless, pinning the outer aggregate emit's qFuzzyCompare
        // gate as the sole settingsChanged source.
        const Row rows[] = {
            {"P->P", kindP, seedProp, kindP, kindP, seedProp, 0, 0, 0},
            {"P->F", kindP, seedProp, kindP, kindF, seededPx, 1, 1, 1},
            {"P->C", kindP, seedProp, kindP, kindC, seedProp, 1, 1, 0},
            {"F->P", kindF, seedPx, kindF, kindP, defaultProp, 1, 1, 1},
            {"F->F", kindF, seedPx, kindF, kindF, seedPx, 0, 0, 0},
            {"F->C", kindF, seedPx, kindF, kindC, seedPx, 1, 1, 0},
            {"C->P proportion stored", kindP, seedProp, kindC, kindP, seedProp, 1, 1, 0},
            {"C->P pixels stored", kindF, seedPx, kindC, kindP, defaultProp, 1, 1, 1},
            {"C->F proportion stored", kindP, seedProp, kindC, kindF, seededPx, 1, 1, 1},
            {"C->F 800px stored", kindF, seededPx, kindC, kindF, seededPx, 1, 1, 0},
            {"C->F 1200px retained", kindF, seedPx, kindC, kindF, seedPx, 1, 1, 0},
            {"C->C", kindP, seedProp, kindC, kindC, seedProp, 0, 0, 0},
        };

        // Failures accumulate instead of QVERIFY2-ing inside the loop: an
        // in-loop abort would silently skip every later row (the known
        // data-loop trap).
        QStringList failures;
        for (const Row& row : rows) {
            TestHelpers::IsolatedConfigGuard guard;
            Settings settings;
            settings.setScrollingDefaultColumnWidthKind(row.seedKind);
            settings.setScrollingDefaultColumnWidthValue(row.seedValue);
            if (settings.scrollingDefaultColumnWidthKind() != row.fromKind) {
                settings.setScrollingDefaultColumnWidthKind(row.fromKind);
            }
            if (settings.scrollingDefaultColumnWidthKind() != row.fromKind) {
                failures.append(QStringLiteral("%1: seed failed").arg(QLatin1String(row.name)));
                continue;
            }

            QSignalSpy changedSpy(&settings, &Settings::settingsChanged);
            QSignalSpy kindSpy(&settings, &Settings::scrollingDefaultColumnWidthKindChanged);
            QSignalSpy valueSpy(&settings, &Settings::scrollingDefaultColumnWidthValueChanged);
            // Emit-ORDER pin: at kindChanged emission time the store must
            // already read the NEW kind while the value still holds the
            // SEED (the flip is announced BEFORE any coercion) — reverting
            // the pass-5 hoist fails this on every coercing row.
            const qreal seededValue = settings.scrollingDefaultColumnWidthValue();
            bool orderOk = true;
            const auto orderConn = QObject::connect(
                &settings, &Settings::scrollingDefaultColumnWidthKindChanged,
                [&settings, &orderOk, &row, seededValue]() {
                    orderOk = orderOk && settings.scrollingDefaultColumnWidthKind() == row.toKind
                        && qFuzzyCompare(1.0 + settings.scrollingDefaultColumnWidthValue(), 1.0 + seededValue);
                });
            settings.setScrollingDefaultColumnWidthKind(row.toKind);
            QObject::disconnect(orderConn);
            if (!orderOk) {
                failures.append(
                    QStringLiteral("%1: kindChanged emitted after the value coercion").arg(QLatin1String(row.name)));
            }
            const qreal actual = settings.scrollingDefaultColumnWidthValue();
            if (settings.scrollingDefaultColumnWidthKind() != row.toKind) {
                failures.append(QStringLiteral("%1: kind not applied").arg(QLatin1String(row.name)));
            }
            if (!qFuzzyCompare(1.0 + actual, 1.0 + row.expectedValue)) {
                failures.append(QStringLiteral("%1: value %2, expected %3")
                                    .arg(QLatin1String(row.name))
                                    .arg(actual)
                                    .arg(row.expectedValue));
            }
            if (changedSpy.count() != row.expectedChanged) {
                failures.append(QStringLiteral("%1: settingsChanged %2, expected %3")
                                    .arg(QLatin1String(row.name))
                                    .arg(changedSpy.count())
                                    .arg(row.expectedChanged));
            }
            if (kindSpy.count() != row.expectedKind) {
                failures.append(QStringLiteral("%1: kindChanged %2, expected %3")
                                    .arg(QLatin1String(row.name))
                                    .arg(kindSpy.count())
                                    .arg(row.expectedKind));
            }
            if (valueSpy.count() != row.expectedValueSig) {
                failures.append(QStringLiteral("%1: valueChanged %2, expected %3")
                                    .arg(QLatin1String(row.name))
                                    .arg(valueSpy.count())
                                    .arg(row.expectedValueSig));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1String("; "))));
    }

    // ── normalizeScrollingColumnWidthValue ──────────────────────────────
    //
    // The width VALUE and KIND are two independent keys, and the schema's
    // clampDouble has to span BOTH kinds' ranges (0.05 proportion through
    // 10000 px), so it cannot reject a pair that is individually legal and
    // jointly nonsense. Three paths write the store without passing the
    // kind-aware setter — hand edit, config import, profile staging — and the
    // repair is a RE-SEED, not a clamp: clamping a Proportion-kind 800 down
    // to 1.0 would open every column at 100% of the work area, and clamping a
    // Fixed-kind 0.5 up to the 100px floor would silently invent a width the
    // user never chose.

    void widthValueNormalizesOnLoad_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<double>("stored");
        QTest::addColumn<double>("expected");

        // A proportion-magnitude value under Fixed cannot be a pixel count.
        QTest::newRow("fixed-holding-a-proportion")
            << ConfigDefaults::scrollingWidthKindFixed() << 0.5 << ConfigDefaults::scrollingDefaultColumnWidthFixedPx();
        // A pixel-magnitude value under Proportion cannot be a fraction.
        QTest::newRow("proportion-holding-pixels") << ConfigDefaults::scrollingWidthKindProportion() << 800.0
                                                   << ConfigDefaults::scrollingDefaultColumnWidthValue();
        // A legal pair is left completely alone, both ways round.
        QTest::newRow("fixed-in-range") << ConfigDefaults::scrollingWidthKindFixed() << 640.0 << 640.0;
        QTest::newRow("proportion-in-range") << ConfigDefaults::scrollingWidthKindProportion() << 0.25 << 0.25;
        // NO rows for "in-kind but out of RANGE". They would be vacuous: the
        // schema's clampDouble(ValueMin, FixedMax) validator runs on the READ
        // path as well as the write (PhosphorConfig::Schema), so a hand-edited
        // Fixed=50000 is already 10000 by the time the getter returns it and
        // the normalizer sees an in-range value. reseedColumnWidthForKind's
        // clamp tail is therefore unreachable defence, not a live path, and a
        // row asserting the clamped result would pass with the tail replaced
        // by `return value;` — coverage the test does not have.
        //
        // ClientDecides ignores the value entirely, so nothing is repaired.
        // The stored value is deliberately PIXEL-magnitude: with 0.5 this row
        // passed whether or not the early return existed, because the
        // proportion arm would have left 0.5 alone too. 800 survives ONLY if
        // the return is really there, and it is the real-world case — a pixel
        // width parked through a ClientDecides hop, which the kind-transition
        // table's "C→F 1200px retained" row depends on surviving.
        QTest::newRow("client-decides-untouched")
            << ConfigDefaults::scrollingWidthKindClientDecides() << 800.0 << 800.0;
    }

    void widthValueNormalizesOnLoad()
    {
        QFETCH(int, kind);
        QFETCH(double, stored);
        QFETCH(double, expected);

        TestHelpers::IsolatedConfigGuard guard;
        const QString configFile = guard.configPath() + QStringLiteral("/plasmazones/config.json");
        {
            // Materialise a real config file, then hand-edit the pair into it
            // exactly as a user or a shared blob could. Going through the
            // setters is impossible by construction: they are what maintains
            // the invariant this repair exists to restore.
            Settings seed;
            seed.save();
        }
        QFile file(configFile);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(configFile));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QVERIFY2(!root.isEmpty(), "seed Settings::save() wrote no config");

        QJsonObject group = root.value(ConfigDefaults::scrollingGroup()).toObject();
        group[ConfigDefaults::defaultColumnWidthKindKey()] = kind;
        group[ConfigDefaults::defaultColumnWidthValueKey()] = stored;
        root[ConfigDefaults::scrollingGroup()] = group;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();

        Settings settings;
        QCOMPARE(settings.scrollingDefaultColumnWidthKind(), kind);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), expected);

        // Idempotent: a second load sees the already-repaired pair, so it must
        // not write and must not re-announce. This is the Discard-reload case,
        // where nothing about the user's view has changed.
        QSignalSpy valueSpy(&settings, &Settings::scrollingDefaultColumnWidthValueChanged);
        settings.load();
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), expected);
        QCOMPARE(valueSpy.count(), 0);
    }

    void widthValueNormalizesOnProfileStaging()
    {
        // applyConfigOverlayStaged writes through importFromJson with no
        // load(), so it needs the repair independently. Without it the engine
        // takes its Fixed branch on a proportion-magnitude value and computes
        // qMax(1, qRound(0.5)) — a ONE PIXEL column — for the whole session,
        // because nothing re-reads the config until the next restart.
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        settings.setScrollingDefaultColumnWidthKind(ConfigDefaults::scrollingWidthKindProportion());
        settings.setScrollingDefaultColumnWidthValue(0.5);

        QJsonObject blob = settings.exportConfigToJson();
        QJsonObject group = blob.value(ConfigDefaults::scrollingGroup()).toObject();
        group[ConfigDefaults::defaultColumnWidthKindKey()] = ConfigDefaults::scrollingWidthKindFixed();
        group[ConfigDefaults::defaultColumnWidthValueKey()] = 0.5;
        blob[ConfigDefaults::scrollingGroup()] = group;

        QVERIFY(settings.applyConfigOverlayStaged(blob));
        QCOMPARE(settings.scrollingDefaultColumnWidthKind(), ConfigDefaults::scrollingWidthKindFixed());
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), ConfigDefaults::scrollingDefaultColumnWidthFixedPx());
    }
};

// QTEST_MAIN (not GUILESS): the transition-table test constructs Settings,
// whose load path reads QGuiApplication::palette().
QTEST_MAIN(TestScrollingSettings)
#include "test_scrolling_settings.moc"
