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
 * become the nearest enumerator), the numeric-range backstops that DO clamp
 * (preset indices, the fixed window height, the adjust-step percents), and
 * the preset-list numeric canonicalizer.
 *
 * The largest family here is the default column WIDTH, whose kind and value
 * are two independent keys with one meaning, so it needs five slots of its
 * own: the schema-range backstop on the shared value key, the kind-aware
 * clamp the setter applies on top of it, the kind-transition table (which
 * flips re-seed the value and which leave it alone, and how many signals each
 * flip may emit), and the two repair paths for a pair that reached the store
 * without passing the setter, normalize-on-load and normalize-on-profile-
 * staging. The default window HEIGHT deliberately has no equivalent: its value
 * key serves one kind (Fixed) with no cross-domain pair to go inconsistent, so
 * a plain clampDouble is the whole story and there is no height normalizer to
 * mirror.
 */

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSignalSpy>
#include <QTest>
#include <QtNumeric>

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

        const auto* heightKind = findKey(schema, group, ConfigDefaults::defaultWindowHeightKindKey());
        QVERIFY(heightKind && heightKind->validator);
        QCOMPARE(heightKind->validator(9).toInt(), ConfigDefaults::scrollingDefaultWindowHeightKind());
        QCOMPARE(heightKind->validator(ConfigDefaults::scrollingHeightKindFixed()).toInt(),
                 ConfigDefaults::scrollingHeightKindFixed());
        QCOMPARE(heightKind->validator(ConfigDefaults::scrollingHeightKindPreset()).toInt(),
                 ConfigDefaults::scrollingHeightKindPreset());
        // 3 is the first value past the closed set {Auto, Fixed, Preset}; a
        // clamping validator would hand it back as Preset.
        QCOMPARE(heightKind->validator(ConfigDefaults::scrollingHeightKindPreset() + 1).toInt(),
                 ConfigDefaults::scrollingDefaultWindowHeightKind());

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

    /// The Scrolling group's numeric-range keys, which DO clamp (clampInt /
    /// clampDouble) rather than falling back to the default like the enums
    /// above: both preset indices and the fixed window height. Both ends of
    /// every range are pinned, and the width and height twins are pinned
    /// identically so neither can quietly lose a bound the other keeps.
    /// The three view bools ship their ConfigDefaults default here too, since
    /// nothing else in the group's schema declaration pins them.
    void scrollingNumericRangesClamp()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::scrollingGroup();
        // The floor is 0 by construction (an index into a list), so it has no
        // ConfigDefaults accessor of its own. Named here anyway so the two
        // ends of the range read symmetrically.
        constexpr int presetIndexMin = 0;

        const auto* widthPresetIdx = findKey(schema, group, ConfigDefaults::defaultColumnWidthPresetIndexKey());
        QVERIFY(widthPresetIdx && widthPresetIdx->validator);
        QCOMPARE(widthPresetIdx->validator(-1).toInt(), presetIndexMin);
        QCOMPARE(widthPresetIdx->validator(99).toInt(), ConfigDefaults::scrollingPresetIndexMax());
        QCOMPARE(widthPresetIdx->defaultValue.toInt(), ConfigDefaults::scrollingDefaultColumnWidthPresetIndex());

        const auto* heightPresetIdx = findKey(schema, group, ConfigDefaults::defaultWindowHeightPresetIndexKey());
        QVERIFY(heightPresetIdx && heightPresetIdx->validator);
        QCOMPARE(heightPresetIdx->validator(-1).toInt(), presetIndexMin);
        QCOMPARE(heightPresetIdx->validator(99).toInt(), ConfigDefaults::scrollingPresetIndexMax());
        QCOMPARE(heightPresetIdx->defaultValue.toInt(), ConfigDefaults::scrollingDefaultWindowHeightPresetIndex());

        const auto* heightValue = findKey(schema, group, ConfigDefaults::defaultWindowHeightValueKey());
        QVERIFY(heightValue && heightValue->validator);
        QCOMPARE(heightValue->validator(1.0).toDouble(), ConfigDefaults::scrollingDefaultWindowHeightMin());
        QCOMPARE(heightValue->validator(99999.0).toDouble(), ConfigDefaults::scrollingDefaultWindowHeightMax());
        QCOMPARE(heightValue->defaultValue.toDouble(), ConfigDefaults::scrollingDefaultWindowHeightValue());

        const auto* tabStrip = findKey(schema, group, ConfigDefaults::tabStripEnabledKey());
        QVERIFY(tabStrip);
        QCOMPARE(tabStrip->defaultValue.toBool(), ConfigDefaults::scrollingTabStripEnabled());
        const auto* wheelEnabled = findKey(schema, group, ConfigDefaults::wheelFocusEnabledKey());
        QVERIFY(wheelEnabled);
        QCOMPARE(wheelEnabled->defaultValue.toBool(), ConfigDefaults::scrollingWheelFocusEnabled());
        const auto* wheelInverted = findKey(schema, group, ConfigDefaults::wheelFocusInvertedKey());
        QVERIFY(wheelInverted);
        QCOMPARE(wheelInverted->defaultValue.toBool(), ConfigDefaults::scrollingWheelFocusInverted());
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
        // The two enum keys' defaultValue is pinned as well as their
        // validator: for an enum the two are independently-spelled
        // expressions (a KeyDef literal and the validIntOr fallback), so a
        // pasted-wrong defaultValue ships a wrong out-of-box value while the
        // validator assertions above still pass.
        QCOMPARE(sticky->defaultValue.toInt(), ConfigDefaults::scrollingStickyWindowHandling());
        const auto* insertPos = findKey(schema, group, ConfigDefaults::insertPositionKey());
        QVERIFY(insertPos);
        QCOMPARE(insertPos->defaultValue.toInt(), ConfigDefaults::scrollingInsertPosition());

        const auto* widthStep = findKey(schema, group, ConfigDefaults::columnWidthStepPercentKey());
        QVERIFY(widthStep && widthStep->validator);
        QCOMPARE(widthStep->validator(0).toInt(), ConfigDefaults::scrollingStepPercentMin());
        QCOMPARE(widthStep->validator(999).toInt(), ConfigDefaults::scrollingStepPercentMax());
        QCOMPARE(widthStep->defaultValue.toInt(), ConfigDefaults::scrollingColumnWidthStepPercent());

        const auto* heightStep = findKey(schema, group, ConfigDefaults::windowHeightStepPercentKey());
        QVERIFY(heightStep && heightStep->validator);
        QCOMPARE(heightStep->validator(-5).toInt(), ConfigDefaults::scrollingStepPercentMin());
        QCOMPARE(heightStep->validator(999).toInt(), ConfigDefaults::scrollingStepPercentMax());
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
        const auto* restoreFloated = findKey(schema, group, ConfigDefaults::restoreFloatedOnLoginKey());
        QVERIFY(restoreFloated);
        QCOMPARE(restoreFloated->defaultValue.toBool(), ConfigDefaults::scrollingRestoreFloatedWindowsOnLogin());
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
        // to the default, which IS a change from IgnoreAll — so it announces
        // itself like any other change. A setter that compared the value it
        // was HANDED (42) rather than the value the store came back with
        // would leave the page showing IgnoreAll over a stored default.
        settings.setScrollingStickyWindowHandling(42);
        QCOMPARE(settings.scrollingStickyWindowHandling(), ConfigDefaults::scrollingStickyWindowHandling());
        QCOMPARE(stickySpy.count(), 2);
        QCOMPARE(changedSpy.count(), 2);

        QSignalSpy stepSpy(&settings, &Settings::scrollingColumnWidthStepPercentChanged);
        const int preChanged = changedSpy.count();
        settings.setScrollingColumnWidthStepPercent(25);
        QCOMPARE(settings.scrollingColumnWidthStepPercent(), 25);
        QCOMPARE(stepSpy.count(), 1);
        QCOMPARE(changedSpy.count(), preChanged + 1);
        // Clamped write: 999 stores as the max, and the clamped result is a
        // change from 25, so it announces itself too.
        settings.setScrollingColumnWidthStepPercent(999);
        QCOMPARE(settings.scrollingColumnWidthStepPercent(), ConfigDefaults::scrollingStepPercentMax());
        QCOMPARE(stepSpy.count(), 2);
        QCOMPARE(changedSpy.count(), preChanged + 2);

        // The height step is the width step's twin and gets the same
        // treatment: an in-range write, then a clamped one, both announced.
        QSignalSpy heightStepSpy(&settings, &Settings::scrollingWindowHeightStepPercentChanged);
        const int preHeightChanged = changedSpy.count();
        settings.setScrollingWindowHeightStepPercent(30);
        QCOMPARE(settings.scrollingWindowHeightStepPercent(), 30);
        QCOMPARE(heightStepSpy.count(), 1);
        QCOMPARE(changedSpy.count(), preHeightChanged + 1);
        settings.setScrollingWindowHeightStepPercent(-5);
        QCOMPARE(settings.scrollingWindowHeightStepPercent(), ConfigDefaults::scrollingStepPercentMin());
        QCOMPARE(heightStepSpy.count(), 2);
        QCOMPARE(changedSpy.count(), preHeightChanged + 2);
    }

    /// Preset lists canonicalize to numeric proportions in (0, 1]: junk and
    /// out-of-range entries are dropped, duplicates collapse to one entry
    /// (comparing canonical spellings, so "0.50" and "0.5" are the same
    /// preset), and the surviving entries keep their stored ORDER, so the
    /// stored value always equals the effective one and a stored preset index
    /// keeps pointing at the same preset (the engine silently ignores
    /// anything else).
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
        // Both ends of that (0, 1] rule, which nothing else pins: 1.0 is the
        // full-width preset and must SURVIVE, 0 is not a width at all and must
        // be DROPPED. Widening the bounds either way passes every other row
        // here while silently dropping every full-width preset.
        QCOMPARE(widths->validator(QStringLiteral("0, 1.0, 0.5")).toString(), QStringLiteral("1,0.5"));
        // Order is preserved, never sorted: the stored preset INDEX points
        // into this list, so a sort would silently remap every user's
        // remembered preset. Every other input here is already ascending, so
        // this descending row is the only thing a sort regression fails.
        QCOMPARE(widths->validator(QStringLiteral("0.5, 0.25")).toString(), QStringLiteral("0.5,0.25"));
        // De-duplication compares CANONICAL spellings, not raw text, so two
        // spellings of one proportion collapse to a single preset.
        QCOMPARE(widths->validator(QStringLiteral("0.5, 0.50, 0.25")).toString(), QStringLiteral("0.5,0.25"));

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
        // past the setter path — 26 entries canonicalize to at most the cap.
        // Every entry is a legal proportion, so the cap is the only thing
        // trimming. The expected size is spelled as presetIndexMax() + 1
        // rather than as a bare 16: the two numbers are one decision (an index
        // past the last entry a capped list can hold could never resolve), and
        // raising only the list cap would otherwise leave the new slots
        // permanently unreachable with the whole suite still green. This is
        // the runtime half of the static_assert in settingsschema_scrolling.
        QStringList many;
        for (int i = 5; i <= 30; ++i) {
            many.append(QString::number(i / 100.0));
        }
        const QStringList capped = widths->validator(many.join(QLatin1Char(','))).toString().split(QLatin1Char(','));
        QCOMPARE(capped.size(), ConfigDefaults::scrollingPresetIndexMax() + 1);
        // Keep-EARLIEST, not keep-last: the survivors are the first entries of
        // the input, so a user's leading presets outlive an over-long tail.
        QCOMPARE(capped.first(), QStringLiteral("0.05"));
        QCOMPARE(capped.last(), QStringLiteral("0.2"));
    }

    /// The width VALUE key clamps into the schema range (backstop; the
    /// kind-aware clamp lives in the Settings setter).
    void widthValueClampsToSchemaRange()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const auto* value =
            findKey(schema, ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
        QVERIFY(value && value->validator);
        QCOMPARE(value->validator(0.001).toDouble(), ConfigDefaults::scrollingDefaultColumnWidthProportionMin());
        QCOMPARE(value->validator(99999.0).toDouble(), ConfigDefaults::scrollingDefaultColumnWidthFixedMax());
        // A corrupt "nan" in the config pins to the MINIMUM. qBound on NaN is
        // unspecified (every comparison is false, so the result depends on
        // argument order), which is why clampDouble special-cases it; reverting
        // that special case to a plain qBound is invisible without this.
        QCOMPARE(value->validator(qQNaN()).toDouble(), ConfigDefaults::scrollingDefaultColumnWidthProportionMin());
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
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(),
                 ConfigDefaults::scrollingDefaultColumnWidthProportionMin());
        settings.setScrollingDefaultColumnWidthValue(0.35);
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), 0.35);
    }

    /// Kind-transition table for the shared width value key, covering all four
    /// legal kinds. The kind setter owns the coercion (see
    /// setScrollingDefaultColumnWidthKind): each arm re-seeds only when the
    /// stored value cannot belong to the kind being entered — Fixed below the
    /// pixel floor, Proportion above 1.0 — while ClientDecides and Preset
    /// leave the value untouched (Preset resolves through its own index key),
    /// and a same-kind write is a full no-op. A value of either kind therefore
    /// survives a ClientDecides or Preset round trip intact. Each effective
    /// flip must emit settingsChanged exactly ONCE — a double emit runs the
    /// engine's refresh+retile sweep twice per user action.
    void widthKindTransitionsCoerceAndEmitOnce()
    {
        const int kindP = ConfigDefaults::scrollingWidthKindProportion();
        const int kindF = ConfigDefaults::scrollingWidthKindFixed();
        const int kindC = ConfigDefaults::scrollingWidthKindClientDecides();
        const int kindR = ConfigDefaults::scrollingWidthKindPreset();
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
        // Every kind pair that carries a distinct rule, plus the pairs whose
        // outcome depends on WHAT a value-preserving hop left stored: C→P and
        // C→F appear more than once because ClientDecides deliberately leaves
        // the value key untouched, so both a proportion and a pixel count can
        // be sitting there when the next flip lands. Preset is the second
        // value-preserving kind and gets the same treatment from the other
        // side — into it, out of it retaining, and out of it re-seeding.
        // "C->F 1200px retained" is
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
            // Preset, the second value-preserving kind: entering it leaves a
            // pixel width alone, leaving it for Fixed hands that same width
            // back, and leaving it for Proportion re-seeds because pixels
            // cannot be a fraction.
            {"F->Preset", kindF, seedPx, kindF, kindR, seedPx, 1, 1, 0},
            {"Preset->F pixels retained", kindF, seedPx, kindR, kindF, seedPx, 1, 1, 0},
            {"Preset->P pixels stored", kindF, seedPx, kindR, kindP, defaultProp, 1, 1, 1},
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
        // Preset is the OTHER kind that stores no width of its own (it
        // resolves through its index key), so it is untouched for the same
        // reason. Its own row rather than a variation of the ClientDecides
        // one: the two are separate arms of the normalizer's guard, and
        // deleting the Preset arm alone re-seeds a retained pixel width while
        // the ClientDecides row above still passes.
        QTest::newRow("preset-untouched") << ConfigDefaults::scrollingWidthKindPreset() << 800.0 << 800.0;
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
        // not touch the file and must not re-announce. This is the
        // Discard-reload case, where nothing about the user's view has
        // changed. The file bytes are compared rather than only the signal,
        // because a repair that ran again and wrote the same value back would
        // be silent on the signal alone.
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray beforeReload = file.readAll();
        file.close();
        QSignalSpy valueSpy(&settings, &Settings::scrollingDefaultColumnWidthValueChanged);
        settings.load();
        QCOMPARE(settings.scrollingDefaultColumnWidthValue(), expected);
        QCOMPARE(valueSpy.count(), 0);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray afterReload = file.readAll();
        file.close();
        QCOMPARE(afterReload, beforeReload);
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
