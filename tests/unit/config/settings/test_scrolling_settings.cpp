// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_settings.cpp
 * @brief Schema-level guards for the Tiling.Scrolling group and the
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
        const QString group = ConfigDefaults::tilingScrollingGroup();

        const auto* center = findKey(schema, group, ConfigDefaults::centerFocusedColumnKey());
        QVERIFY(center && center->validator);
        QCOMPARE(center->validator(99).toInt(), ConfigDefaults::scrollingCenterFocusedColumn());
        QCOMPARE(center->validator(2).toInt(), 2); // legal value passes through

        const auto* kind = findKey(schema, group, ConfigDefaults::defaultColumnWidthKindKey());
        QVERIFY(kind && kind->validator);
        QCOMPARE(kind->validator(-1).toInt(), ConfigDefaults::scrollingDefaultColumnWidthKind());
        QCOMPARE(kind->validator(2).toInt(), 2);

        const auto* display = findKey(schema, group, ConfigDefaults::defaultColumnDisplayKey());
        QVERIFY(display && display->validator);
        QCOMPARE(display->validator(7).toInt(), ConfigDefaults::scrollingDefaultColumnDisplay());
        QCOMPARE(display->validator(1).toInt(), 1);
    }

    /// Preset lists canonicalize to numeric proportions in (0, 1]: junk and
    /// out-of-range entries are dropped so the stored value always equals
    /// the effective one (the engine silently ignores anything else).
    void presetListsCanonicalizeNumerically()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::tilingScrollingGroup();

        const auto* widths = findKey(schema, group, ConfigDefaults::presetColumnWidthsKey());
        QVERIFY(widths && widths->validator);
        QCOMPARE(widths->validator(QStringLiteral("0.25, abc, 5, 0.5, 0.5, -1")).toString(),
                 QStringLiteral("0.25,0.5"));
        // The shipped default survives its own validator UNCHANGED — a
        // non-empty result is not enough: a validator that silently dropped
        // or reordered entries would still pass an isEmpty check while the
        // stored list stopped matching the shipped one.
        QCOMPARE(widths->validator(widths->defaultValue).toString(), ConfigDefaults::scrollingPresetColumnWidths());
        // Entries below the scalar width key's floor are dropped, not kept:
        // the setter would clamp them away downstream, so accepting them here
        // stores a preset the engine will never open a column at.
        QCOMPARE(widths->validator(QStringLiteral("0.01, 0.5")).toString(), QStringLiteral("0.5"));
        // A list of ONLY sub-floor entries snaps to the default, like any
        // other nothing-survives input.
        QCOMPARE(widths->validator(QStringLiteral("0.01, 0.02")).toString(),
                 ConfigDefaults::scrollingPresetColumnWidths());
        // The floor is inclusive — the minimum itself is a legal preset.
        QCOMPARE(widths->validator(QString::number(ConfigDefaults::scrollingDefaultColumnWidthValueMin())).toString(),
                 QString::number(ConfigDefaults::scrollingDefaultColumnWidthValueMin()));

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
        // entry starts at the floor (0.05) so the cap, not the floor, is what
        // does the trimming.
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
            findKey(schema, ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
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
    /// entering Fixed re-seeds a pixel width, entering Proportion re-seeds
    /// only when pixels are stored (directly from Fixed or via a
    /// ClientDecides hop), ClientDecides leaves the value untouched, and a
    /// same-kind write is a full no-op. Each effective flip must emit
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
        // All nine kind pairs; C→P and C→F appear twice because their
        // outcome depends on WHAT the ClientDecides hop left stored (it
        // deliberately leaves the value key untouched, so both a proportion
        // and a pixel count can be sitting there). "C->F 800px stored" is
        // the one transition where the coercion branch runs but the nested
        // value setter is a no-op — the OUTER aggregate emit is the sole
        // settingsChanged source there, pinning the qFuzzyCompare gate.
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
};

// QTEST_MAIN (not GUILESS): the transition-table test constructs Settings,
// whose load path reads QGuiApplication::palette().
QTEST_MAIN(TestScrollingSettings)
#include "test_scrolling_settings.moc"
