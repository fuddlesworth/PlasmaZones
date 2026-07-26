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
#include <QTest>

#include <PhosphorConfig/Schema.h>

#include "config/configdefaults.h"
#include "config/settingsschema.h"

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
        // Home/PgUp are fine). A symbol spelled as a NAMED key ("Plus")
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
        // The shipped default survives its own validator.
        QVERIFY(!widths->validator(widths->defaultValue).toString().isEmpty());

        const auto* heights = findKey(schema, group, ConfigDefaults::presetWindowHeightsKey());
        QVERIFY(heights && heights->validator);
        QCOMPARE(heights->validator(QStringLiteral("")).toString(), QString());
        QVERIFY(!heights->validator(heights->defaultValue).toString().isEmpty());
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
        QCOMPARE(value->validator(99999.0).toDouble(), ConfigDefaults::scrollingDefaultColumnWidthValueMax());
    }
};

QTEST_GUILESS_MAIN(TestScrollingSettings)
#include "test_scrolling_settings.moc"
