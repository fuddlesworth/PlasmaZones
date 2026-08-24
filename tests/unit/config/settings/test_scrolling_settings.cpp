// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): this file is well past the 1150 hard
// ceiling. The case for it: the shortcut-invariant guards (duplicate
// defaults, Shift+symbol spellings, the advertised-chord value pins) and the
// scrolling schema guards read each other's fixtures and pin the
// SAME defaults table, so a split would duplicate the advertised-chord
// pins across two files and let them drift apart — the exact defect class
// this suite exists to prevent. The tab-label typography guards belong to
// that same table rather than being a new concern: the font weight's clamp
// is only legible beside the tab-indicator enum rows it must not be
// confused with, and the family's deliberately absent validator is the
// counterpart of the colour trio's present one two blocks above. If a
// genuinely separate concern lands, it takes a sibling rather than growing
// this.

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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QList>
#include <QPair>
#include <QSignalSpy>
#include <QTest>
#include <QtNumeric>

#include <utility>

#include <PhosphorConfig/Schema.h>
// For the drop indicator's radius default, which is deliberately the zone
// overlay's constant rather than a literal.
#include <PhosphorRules/ActionParams.h>
#include <PhosphorZones/ZoneDefaults.h>

#include "config/configdefaults.h"
#include "config/configmigration.h"
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

    /// The specific chords the CHANGELOG advertises, plus the
    /// ships-unbound set, pinned by VALUE. The structural guards above
    /// cannot see a retune that moves an advertised default (or binds a
    /// deliberately-unbound verb) while staying unique and parseable — this
    /// is what fails when a shipped doc claim and a default disagree.
    void advertisedChordValues()
    {
        QCOMPARE(ConfigDefaults::scrollingCenterVisibleColumnsShortcut(), QStringLiteral("Meta+Alt+Shift+C"));
        QCOMPARE(ConfigDefaults::scrollingFocusWindowTopShortcut(), QStringLiteral("Meta+Alt+V"));
        QCOMPARE(ConfigDefaults::scrollingFocusWindowBottomShortcut(), QStringLiteral("Meta+Alt+Shift+V"));
        // Promoted to the mode-neutral shortcut group when every engine
        // gained the verb; the chord itself is unchanged.
        QCOMPARE(ConfigDefaults::switchFocusFloatTilingShortcut(), QStringLiteral("Meta+Alt+X"));
        // The preset cycles page on the paging keys, plain for the width
        // axis and Shift for the height axis.
        QCOMPARE(ConfigDefaults::scrollingCycleColumnWidthShortcut(), QStringLiteral("Meta+Alt+PgUp"));
        QCOMPARE(ConfigDefaults::scrollingCycleColumnWidthBackShortcut(), QStringLiteral("Meta+Alt+PgDown"));
        QCOMPARE(ConfigDefaults::autotileRetileShortcut(), QStringLiteral("Meta+Ctrl+T"));
        // The rest of the CHANGELOG's advertised-by-value chords: the
        // consume/expel pair, the column-width and window-height pairs and
        // windowed fullscreen. A retune of any of these ships a doc that
        // names the old chord unless it fails here.
        QCOMPARE(ConfigDefaults::scrollingConsumeWindowShortcut(), QStringLiteral("Meta+Alt+I"));
        QCOMPARE(ConfigDefaults::scrollingExpelWindowShortcut(), QStringLiteral("Meta+Alt+Shift+I"));
        QCOMPARE(ConfigDefaults::scrollingIncreaseColumnWidthShortcut(), QStringLiteral("Meta+Alt+W"));
        QCOMPARE(ConfigDefaults::scrollingDecreaseColumnWidthShortcut(), QStringLiteral("Meta+Alt+Shift+W"));
        // The height ADJUST pair mirrors the width one on its own letter.
        QCOMPARE(ConfigDefaults::scrollingIncreaseWindowHeightShortcut(), QStringLiteral("Meta+Alt+H"));
        QCOMPARE(ConfigDefaults::scrollingDecreaseWindowHeightShortcut(), QStringLiteral("Meta+Alt+Shift+H"));
        QCOMPARE(ConfigDefaults::scrollingCycleWindowHeightShortcut(), QStringLiteral("Meta+Alt+Shift+PgUp"));
        QCOMPARE(ConfigDefaults::scrollingCycleWindowHeightBackShortcut(), QStringLiteral("Meta+Alt+Shift+PgDown"));
        // Off Meta+Alt+0, where it read as a tenth quick-layout digit; now
        // the Alt twin of Retile's T beside Equalize's Shift twin.
        QCOMPARE(ConfigDefaults::scrollingResetWindowHeightsShortcut(), QStringLiteral("Meta+Ctrl+Alt+T"));
        QCOMPARE(ConfigDefaults::scrollingToggleWindowedFullscreenShortcut(), QStringLiteral("Meta+Alt+Shift+F"));
        // The no-focus PAGE pan, on a free letter of the Meta+Alt pool. The
        // step pan is the wheel's (Meta+Shift+wheel, an effect-side axis
        // shortcut) and has no keyboard row at all.
        QCOMPARE(ConfigDefaults::scrollingViewPageBackShortcut(), QStringLiteral("Meta+Alt+Y"));
        QCOMPARE(ConfigDefaults::scrollingViewPageForwardShortcut(), QStringLiteral("Meta+Alt+Shift+Y"));
        // The two width re-flows ride Shift twins of existing letters: Retile's
        // T (outside the Meta+Alt family) and grow-into-empty-space's E.
        QCOMPARE(ConfigDefaults::scrollingEqualizeColumnWidthsShortcut(), QStringLiteral("Meta+Ctrl+Shift+T"));
        QCOMPARE(ConfigDefaults::scrollingMinimizeColumnWidthShortcut(), QStringLiteral("Meta+Alt+Shift+E"));

        // Ships unbound, per the same docs: the edge-stop/wrap focus
        // variants and the one-way float verbs.
        QVERIFY(ConfigDefaults::scrollingFocusColumnLeftShortcut().isEmpty());
        QVERIFY(ConfigDefaults::scrollingFocusColumnRightShortcut().isEmpty());
        QVERIFY(ConfigDefaults::scrollingFocusColumnLeftOrLastShortcut().isEmpty());
        QVERIFY(ConfigDefaults::scrollingFocusColumnRightOrFirstShortcut().isEmpty());
        QVERIFY(ConfigDefaults::scrollingMoveToFloatingShortcut().isEmpty());
        QVERIFY(ConfigDefaults::scrollingMoveToTilingShortcut().isEmpty());
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

        // ── Scrolling.TabIndicator ──
        // The two enums are closed sets (a stray value snaps back to the
        // default, it does NOT clamp to the nearest member), and the two
        // signed ranges really do admit negatives — the gap because a negative
        // draws the indicator over the window, the corner radius because its
        // floor is the "fully rounded" sentinel.
        const QString tabGroup = ConfigDefaults::scrollingTabIndicatorGroup();

        const auto* style = findKey(schema, tabGroup, ConfigDefaults::styleKey());
        QVERIFY(style && style->validator);
        QCOMPARE(style->defaultValue.toInt(), ConfigDefaults::scrollingTabIndicatorStyle());
        QCOMPARE(style->validator(ConfigDefaults::scrollingTabIndicatorStyleBar()).toInt(),
                 ConfigDefaults::scrollingTabIndicatorStyleBar());
        // BELOW the set, not above. The set is {Chips=0, Bar=1} and the
        // default is Bar, the HIGHER member, so an out-of-set 7 clamps to 1
        // and snaps back to 1 alike — it cannot tell validIntOr from
        // clampInt, which is the whole property this line claims to pin.
        // -5 clamps to Chips and snaps back to Bar.
        QCOMPARE(style->validator(-5).toInt(), ConfigDefaults::scrollingTabIndicatorStyle());
        QCOMPARE(style->validator(7).toInt(), ConfigDefaults::scrollingTabIndicatorStyle());

        const auto* position = findKey(schema, tabGroup, ConfigDefaults::positionKey());
        QVERIFY(position && position->validator);
        QCOMPARE(position->defaultValue.toInt(), ConfigDefaults::scrollingTabIndicatorPosition());
        QCOMPARE(position->validator(ConfigDefaults::scrollingTabIndicatorPositionLeft()).toInt(),
                 ConfigDefaults::scrollingTabIndicatorPositionLeft());
        // One past the closed set {Left, Right, Top, Bottom}: a clamping
        // validator would hand this back as Bottom.
        QCOMPARE(position->validator(ConfigDefaults::scrollingTabIndicatorPositionBottom() + 1).toInt(),
                 ConfigDefaults::scrollingTabIndicatorPosition());

        const auto* gap = findKey(schema, tabGroup, ConfigDefaults::gapKey());
        QVERIFY(gap && gap->validator);
        QCOMPARE(gap->defaultValue.toInt(), ConfigDefaults::scrollingTabIndicatorGap());
        // A negative gap SURVIVES the clamp — this is the assertion that would
        // catch someone "fixing" the floor to 0.
        QCOMPARE(gap->validator(-10).toInt(), -10);
        QCOMPARE(gap->validator(-9999).toInt(), ConfigDefaults::scrollingTabIndicatorGapMin());
        QCOMPARE(gap->validator(9999).toInt(), ConfigDefaults::scrollingTabIndicatorGapMax());

        // GapsBetweenTabs sits two entries from Gap and looks identical, but
        // its floor is 0, not negative. A copy-paste from the block above is
        // exactly how it would acquire a negative floor it must not have.
        const auto* betweenTabs = findKey(schema, tabGroup, ConfigDefaults::gapsBetweenTabsKey());
        QVERIFY(betweenTabs && betweenTabs->validator);
        QCOMPARE(betweenTabs->defaultValue.toInt(), ConfigDefaults::scrollingTabIndicatorGapsBetweenTabs());
        QCOMPARE(betweenTabs->validator(-10).toInt(), ConfigDefaults::scrollingTabIndicatorGapsBetweenTabsMin());
        QCOMPARE(betweenTabs->validator(9999).toInt(), ConfigDefaults::scrollingTabIndicatorGapsBetweenTabsMax());

        // The two bools carry no validator; the assertion that matters is that
        // they are present with the shipped default, since the whole family
        // has to be reachable through the schema for reset/discard to cover it.
        for (const auto& pair : QList<QPair<QString, bool>>{
                 {ConfigDefaults::hideWhenSingleTabKey(), ConfigDefaults::scrollingTabIndicatorHideWhenSingleTab()},
                 {ConfigDefaults::placeWithinColumnKey(), ConfigDefaults::scrollingTabIndicatorPlaceWithinColumn()}}) {
            const auto* entry = findKey(schema, tabGroup, pair.first);
            QVERIFY2(entry, qPrintable(pair.first));
            QCOMPARE(entry->defaultValue.toBool(), pair.second);
        }

        const auto* radius = findKey(schema, tabGroup, ConfigDefaults::cornerRadiusKey());
        QVERIFY(radius && radius->validator);
        QCOMPARE(radius->defaultValue.toInt(), ConfigDefaults::scrollingTabIndicatorCornerRadius());
        // The shipped default is SQUARE (niri's), not the pill sentinel — the
        // sentinel is a value the user opts into.
        QCOMPARE(radius->defaultValue.toInt(), 0);
        // The pill sentinel survives; nothing below it does.
        QCOMPARE(radius->validator(ConfigDefaults::scrollingTabIndicatorCornerRadiusPill()).toInt(),
                 ConfigDefaults::scrollingTabIndicatorCornerRadiusPill());
        QCOMPARE(radius->validator(-50).toInt(), ConfigDefaults::scrollingTabIndicatorCornerRadiusPill());

        const auto* width = findKey(schema, tabGroup, ConfigDefaults::widthKey());
        QVERIFY(width && width->validator);
        QCOMPARE(width->defaultValue.toInt(), ConfigDefaults::scrollingTabIndicatorWidth());
        QCOMPARE(width->validator(0).toInt(), ConfigDefaults::scrollingTabIndicatorWidthMin());
        // Both ends, like the two gap entries: a widened ceiling would
        // otherwise go unnoticed here.
        QCOMPARE(width->validator(9999).toInt(), ConfigDefaults::scrollingTabIndicatorWidthMax());

        const auto* length = findKey(schema, tabGroup, ConfigDefaults::lengthProportionKey());
        QVERIFY(length && length->validator);
        QCOMPARE(length->defaultValue.toDouble(), ConfigDefaults::scrollingTabIndicatorLengthProportion());
        QCOMPARE(length->validator(0.0).toDouble(), ConfigDefaults::scrollingTabIndicatorLengthProportionMin());
        QCOMPARE(length->validator(5.0).toDouble(), ConfigDefaults::scrollingTabIndicatorLengthProportionMax());

        // ── the label font ──
        // The three style flags are plain bools with nothing to validate. The
        // weight is a ranged key and it CLAMPS rather than snapping back — a
        // hand-edited 9999 is a user asking for the boldest face, not junk.
        // The family is free-form (EMPTY means the system font, which no
        // closed set can express alongside an arbitrary installed family), but
        // it is canonicalised: trimmed, and snapped back to empty when it is
        // implausibly long.
        const auto* fontFamily = findKey(schema, tabGroup, ConfigDefaults::fontFamilyKey());
        QVERIFY(fontFamily && fontFamily->validator);
        QCOMPARE(fontFamily->defaultValue.toString(), ConfigDefaults::scrollingTabIndicatorFontFamily());
        // The shipped default IS the empty sentinel, which is what makes the
        // page's explicit-empty reset write meaningful rather than decorative.
        QVERIFY(fontFamily->defaultValue.toString().isEmpty());
        // A real family survives untouched.
        QCOMPARE(fontFamily->validator(QStringLiteral("Noto Sans")).toString(), QStringLiteral("Noto Sans"));
        // Trimmed, which is the load-bearing half: a padded value is NOT the
        // empty sentinel, so without this it would pass an isEmpty() check and
        // reach QFont::setFamily, which substitutes an arbitrary face rather
        // than the system font the user asked for.
        QCOMPARE(fontFamily->validator(QStringLiteral("  Noto Sans  ")).toString(), QStringLiteral("Noto Sans"));
        QCOMPARE(fontFamily->validator(QStringLiteral("   ")).toString(), QString());
        // Empty stays empty rather than being treated as a length failure.
        QCOMPARE(fontFamily->validator(QString()).toString(), QString());
        // Implausibly long SNAPS to the system font rather than truncating,
        // because a truncated family names a face that does not exist and Qt
        // would substitute something arbitrary for it.
        const QString overlong(PhosphorRules::MaxFontFamilyLength + 1, QLatin1Char('x'));
        QCOMPARE(fontFamily->validator(overlong).toString(), QString());
        // …and the boundary itself is accepted, so the cap is off-by-one safe.
        const QString atCap(PhosphorRules::MaxFontFamilyLength, QLatin1Char('x'));
        QCOMPARE(fontFamily->validator(atCap).toString(), atCap);

        const auto* fontWeight = findKey(schema, tabGroup, ConfigDefaults::fontWeightKey());
        QVERIFY(fontWeight && fontWeight->validator);
        QCOMPARE(fontWeight->defaultValue.toInt(), ConfigDefaults::scrollingTabIndicatorFontWeight());
        QCOMPARE(fontWeight->validator(-5).toInt(), ConfigDefaults::scrollingTabIndicatorFontWeightMin());
        QCOMPARE(fontWeight->validator(9999).toInt(), ConfigDefaults::scrollingTabIndicatorFontWeightMax());
        // The identity row is the one that tells clampInt from validIntOr: 400
        // is a perfectly legal weight that is NOT the default, so a validator
        // swapped for the snap-back kind would hand back the default here while
        // still passing both clamp rows above.
        QCOMPARE(fontWeight->validator(400).toInt(), 400);

        // The three flags carry no validator, like the two bools above; what
        // matters is that they are reachable through the SCHEMA at all, since
        // that is what carries them into reset, discard and the profile blobs.
        for (const auto& pair : QList<QPair<QString, bool>>{
                 {ConfigDefaults::fontItalicKey(), ConfigDefaults::scrollingTabIndicatorFontItalic()},
                 {ConfigDefaults::fontUnderlineKey(), ConfigDefaults::scrollingTabIndicatorFontUnderline()},
                 {ConfigDefaults::fontStrikeoutKey(), ConfigDefaults::scrollingTabIndicatorFontStrikeout()}}) {
            const auto* entry = findKey(schema, tabGroup, pair.first);
            QVERIFY2(entry, qPrintable(pair.first));
            QCOMPARE(entry->defaultValue.toBool(), pair.second);
        }

        // The colours carry canonicalThemeFallbackColor (not a closed set —
        // EMPTY is the meaningful "follow the theme" value). Pin the
        // validator like the drop-indicator loop below does: it is the DISK
        // path's only guard, and without these compares deleting it from the
        // schema would leave the suite green while junk reached QML as an
        // invalid QColor and painted black.
        const std::pair<QString, QString> colourPins[] = {
            {ConfigDefaults::activeColorKey(), ConfigDefaults::scrollingTabIndicatorActiveColor()},
            {ConfigDefaults::inactiveColorKey(), ConfigDefaults::scrollingTabIndicatorInactiveColor()},
            {ConfigDefaults::urgentColorKey(), ConfigDefaults::scrollingTabIndicatorUrgentColor()},
        };
        // Accumulate failures rather than QVERIFY-ing in the loop: an
        // in-loop abort would silently skip every later colour key (the
        // known data-loop trap the transition-table test documents).
        QStringList colourFailures;
        for (const auto& [colorKey, defaultColour] : colourPins) {
            const auto* color = findKey(schema, tabGroup, colorKey);
            if (!color) {
                colourFailures << QStringLiteral("%1: key missing from schema").arg(colorKey);
                continue;
            }
            // Pin the schema default to the ConfigDefaults accessor (today
            // the schema reads the accessor directly, so this only guards
            // against the entry being replaced with a literal), and
            // separately pin that it is EMPTY (the "follow the theme" value).
            if (color->defaultValue.toString() != defaultColour || !color->defaultValue.toString().isEmpty()) {
                colourFailures << QStringLiteral("%1: default is '%2', expected the empty sentinel")
                                      .arg(colorKey, color->defaultValue.toString());
            }
            if (!color->validator) {
                colourFailures << QStringLiteral("%1: no validator attached").arg(colorKey);
                continue;
            }
            if (color->validator(QStringLiteral("not-a-colour")).toString() != QString()
                || color->validator(QString()).toString() != QString()
                || color->validator(QStringLiteral("#FF3366CC")).toString() != QStringLiteral("#FF3366CC")) {
                colourFailures << QStringLiteral("%1: validator misbehaves").arg(colorKey);
            }
        }
        QVERIFY2(colourFailures.isEmpty(), qPrintable(colourFailures.join(QStringLiteral("; "))));

        // The old flat Scrolling/TabStripEnabled key is GONE, not aliased: the
        // family moved wholesale into its own group and the no-ad-hoc-compat
        // rule means the stale value is simply dropped.
        QVERIFY(findKey(schema, group, QStringLiteral("TabStripEnabled")) == nullptr);
        const auto* enabled = findKey(schema, tabGroup, ConfigDefaults::enabledKey());
        QVERIFY(enabled);
        QCOMPARE(enabled->defaultValue.toBool(), ConfigDefaults::scrollingTabIndicatorEnabled());

        // ── Scrolling.DropIndicator ──
        // The paint family for the drag re-insert highlight. Six keys, and
        // every one of them was unpinned until this block existed — the group
        // had no test of any kind, while its sibling above is exhaustively
        // covered. Both clamp ends matter here for a reason peculiar to this
        // group: all three of its minima mean "invisible" (transparent fill,
        // no border, no rounding), so a validator that silently floors is the
        // difference between a drawn indicator and a slot that is created,
        // shown, animated and synced every drag while painting nothing.
        const QString dropGroup = ConfigDefaults::scrollingDropIndicatorGroup();

        const auto* dropEnabled = findKey(schema, dropGroup, ConfigDefaults::enabledKey());
        QVERIFY(dropEnabled);
        QCOMPARE(dropEnabled->defaultValue.toBool(), ConfigDefaults::scrollingDropIndicatorEnabled());

        const auto* dropOpacity = findKey(schema, dropGroup, ConfigDefaults::opacityKey());
        QVERIFY(dropOpacity && dropOpacity->validator);
        QCOMPARE(dropOpacity->defaultValue.toDouble(), ConfigDefaults::scrollingDropIndicatorOpacity());
        QCOMPARE(dropOpacity->validator(-1.0).toDouble(), ConfigDefaults::scrollingDropIndicatorOpacityMin());
        QCOMPARE(dropOpacity->validator(2.0).toDouble(), ConfigDefaults::scrollingDropIndicatorOpacityMax());
        // Fully transparent and fully opaque are both LEGAL, not clamped away:
        // 0.0 is edge-only, 1.0 is a solid fill. A clamp that excluded either
        // end would take a real configuration off the table.
        QCOMPARE(dropOpacity->validator(0.0).toDouble(), 0.0);
        QCOMPARE(dropOpacity->validator(1.0).toDouble(), 1.0);

        const auto* dropWidth = findKey(schema, dropGroup, ConfigDefaults::widthKey());
        QVERIFY(dropWidth && dropWidth->validator);
        QCOMPARE(dropWidth->defaultValue.toInt(), ConfigDefaults::scrollingDropIndicatorBorderWidth());
        QCOMPARE(dropWidth->validator(-5).toInt(), ConfigDefaults::scrollingDropIndicatorBorderWidthMin());
        QCOMPARE(dropWidth->validator(9999).toInt(), ConfigDefaults::scrollingDropIndicatorBorderWidthMax());
        // Zero border width is a supported look (fill with no edge), so it has
        // to survive the clamp rather than being floored to 1.
        QCOMPARE(dropWidth->validator(0).toInt(), 0);

        const auto* dropRadius = findKey(schema, dropGroup, ConfigDefaults::radiusKey());
        QVERIFY(dropRadius && dropRadius->validator);
        QCOMPARE(dropRadius->defaultValue.toInt(), ConfigDefaults::scrollingDropIndicatorBorderRadius());
        QCOMPARE(dropRadius->validator(-5).toInt(), ConfigDefaults::scrollingDropIndicatorBorderRadiusMin());
        QCOMPARE(dropRadius->validator(9999).toInt(), ConfigDefaults::scrollingDropIndicatorBorderRadiusMax());
        // The radius default is deliberately the zone overlay's, so the drop
        // highlight and the snap highlight round identically out of the box.
        // Pinned against the shared constant, not a literal 8, so a change
        // upstream moves both or fails here.
        QCOMPARE(dropRadius->defaultValue.toInt(), int(PhosphorZones::ZoneDefaults::BorderRadius));

        // Both colours default EMPTY, which is the "follow the colour scheme"
        // sentinel — the one value a QColor round-trip could not carry, and
        // the reason these two are stored as free-form strings.
        // Accumulated like the tab loop above, for the same in-loop-abort
        // reason; QCOMPARE-style messages come from the failure strings.
        QStringList dropFailures;
        for (const auto& colourKey : {ConfigDefaults::colorKey(), ConfigDefaults::borderColorKey()}) {
            const auto* dropColour = findKey(schema, dropGroup, colourKey);
            if (!dropColour) {
                dropFailures << QStringLiteral("%1: key missing from schema").arg(colourKey);
                continue;
            }
            if (!dropColour->defaultValue.toString().isEmpty()) {
                dropFailures << QStringLiteral("%1: default is '%2', expected the empty sentinel")
                                    .arg(colourKey, dropColour->defaultValue.toString());
            }
            // The DISK path's only guard. The D-Bus setter refuses an
            // unparseable colour, but a hand-edited config never goes through
            // it and reaches QML as an invalid QColor, which Qt paints BLACK
            // rather than falling back to the scheme. Junk must come back as
            // the empty sentinel, and both the sentinel and a real colour
            // must survive untouched.
            if (!dropColour->validator) {
                dropFailures << QStringLiteral("%1: no validator attached").arg(colourKey);
                continue;
            }
            if (dropColour->validator(QStringLiteral("not-a-colour")).toString() != QString()
                || dropColour->validator(QString()).toString() != QString()
                || dropColour->validator(QStringLiteral("#FF3366CC")).toString() != QStringLiteral("#FF3366CC")) {
                dropFailures << QStringLiteral("%1: validator misbehaves").arg(colourKey);
            }
        }
        QVERIFY2(dropFailures.isEmpty(), qPrintable(dropFailures.join(QStringLiteral("; "))));
    }

    /// Scrolling.Behavior.DragScroll, the edge auto-scroll group. Same shape
    /// as the indicator groups above: every default pinned against its
    /// ConfigDefaults accessor, and both ends of every clampInt range pinned,
    /// so a pasted-wrong bound or a default that drifts from ConfigDefaults
    /// fails here instead of shipping. The engine keeps its own copy of these
    /// four defaults as the IScrollSettings bodies, which a static_assert in
    /// settings/scrolling.cpp ties to the ConfigDefaults side — this slot is
    /// what ties the SCHEMA side to it.
    void scrollingDragScrollSchemaValidates()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::scrollingDragScrollGroup();

        const auto* enabled = findKey(schema, group, ConfigDefaults::enabledKey());
        QVERIFY(enabled);
        QCOMPARE(enabled->defaultValue.toBool(), ConfigDefaults::scrollingDragScrollEnabled());

        const auto* triggerWidth = findKey(schema, group, ConfigDefaults::triggerWidthKey());
        QVERIFY(triggerWidth && triggerWidth->validator);
        QCOMPARE(triggerWidth->defaultValue.toInt(), ConfigDefaults::scrollingDragScrollTriggerWidth());
        QCOMPARE(triggerWidth->validator(-5).toInt(), ConfigDefaults::scrollingDragScrollTriggerWidthMin());
        QCOMPARE(triggerWidth->validator(99999).toInt(), ConfigDefaults::scrollingDragScrollTriggerWidthMax());
        // The floor is at least 1, so the UI cannot offer a zero-width band.
        // Pinned directly rather than round-tripped through the validator,
        // which would compare the Min accessor against itself and pass even
        // if the floor were lowered to zero. (The engine does not depend on
        // this: drag_autoscroll.cpp clamps the ramp's divisor to >= 1 itself.)
        QVERIFY(ConfigDefaults::scrollingDragScrollTriggerWidthMin() >= 1);

        const auto* delayMs = findKey(schema, group, ConfigDefaults::delayMsKey());
        QVERIFY(delayMs && delayMs->validator);
        QCOMPARE(delayMs->defaultValue.toInt(), ConfigDefaults::scrollingDragScrollDelayMs());
        QCOMPARE(delayMs->validator(-5).toInt(), ConfigDefaults::scrollingDragScrollDelayMsMin());
        QCOMPARE(delayMs->validator(99999).toInt(), ConfigDefaults::scrollingDragScrollDelayMsMax());
        // Zero IS legal here and means "start immediately", so it must come
        // back untouched rather than being floored to some minimum dwell.
        QCOMPARE(delayMs->validator(0).toInt(), 0);

        const auto* maxSpeed = findKey(schema, group, ConfigDefaults::maxSpeedKey());
        QVERIFY(maxSpeed && maxSpeed->validator);
        QCOMPARE(maxSpeed->defaultValue.toInt(), ConfigDefaults::scrollingDragScrollMaxSpeed());
        QCOMPARE(maxSpeed->validator(0).toInt(), ConfigDefaults::scrollingDragScrollMaxSpeedMin());
        QCOMPARE(maxSpeed->validator(999999).toInt(), ConfigDefaults::scrollingDragScrollMaxSpeedMax());
        // ABSOLUTE floor, not the accessor compared against itself: lowering
        // MaxSpeedMin to 0 would keep the line above green while the UI
        // offered a top speed of zero — a band that arms, owns the target
        // and never moves. (The engine independently clamps to >= 1 px/s in
        // engine_core, so the blast radius is a misleading settings range,
        // not a stuck engine; the floor still belongs pinned.)
        QVERIFY(ConfigDefaults::scrollingDragScrollMaxSpeedMin() >= 1);
    }

    /// The Scrolling group's numeric-range keys, which DO clamp (clampInt /
    /// clampDouble) rather than falling back to the default like the enums
    /// above: both preset indices and the fixed window height. Both ends of
    /// every range are pinned, and the width and height twins are pinned
    /// identically so neither can quietly lose a bound the other keeps.
    /// The group's bools (the two wheel toggles, always-center, crop
    /// straddlers and the mode's enabled flag) ship their ConfigDefaults
    /// default here too, since nothing else in the group's schema
    /// declaration pins them.
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

        const auto* wheelEnabled = findKey(schema, group, ConfigDefaults::wheelFocusEnabledKey());
        QVERIFY(wheelEnabled);
        QCOMPARE(wheelEnabled->defaultValue.toBool(), ConfigDefaults::scrollingWheelFocusEnabled());
        const auto* wheelInverted = findKey(schema, group, ConfigDefaults::wheelFocusInvertedKey());
        QVERIFY(wheelInverted);
        QCOMPARE(wheelInverted->defaultValue.toBool(), ConfigDefaults::scrollingWheelFocusInverted());
        // The two scroll keys live in their own groups, so assert them
        // against those rather than the Scrolling group the switches use.
        const auto* wheelFocusTriggers =
            findKey(schema, ConfigDefaults::scrollingWheelFocusGroup(), ConfigDefaults::triggersKey());
        QVERIFY(wheelFocusTriggers && wheelFocusTriggers->validator);
        QCOMPARE(wheelFocusTriggers->defaultValue, ConfigDefaults::scrollingWheelFocusTriggers());
        const auto* wheelViewTriggers =
            findKey(schema, ConfigDefaults::scrollingWheelViewGroup(), ConfigDefaults::triggersKey());
        QVERIFY(wheelViewTriggers && wheelViewTriggers->validator);
        QCOMPARE(wheelViewTriggers->defaultValue, ConfigDefaults::scrollingWheelViewTriggers());
        // Both defaults must survive their own validator untouched. A default
        // the canonicaliser rewrites would never compare equal to what is
        // stored, and sparse persistence decides what to WRITE by exactly
        // that comparison — the failure mode is a key that reappears on every
        // save.
        QCOMPARE(wheelFocusTriggers->validator(wheelFocusTriggers->defaultValue), wheelFocusTriggers->defaultValue);
        QCOMPARE(wheelViewTriggers->validator(wheelViewTriggers->defaultValue), wheelViewTriggers->defaultValue);
        // Both halves of the stock pair are pinned BY VALUE, not just against
        // their own ConfigDefaults accessor: the invariant is that the two
        // chords differ by exactly one modifier and so cannot shadow each
        // other, and a wiring-only check stays green if either default moves.
        const QVariantMap focusTrigger = wheelFocusTriggers->defaultValue.toList().first().toMap();
        QCOMPARE(focusTrigger.value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::Meta));
        // Meta+Shift is the whole reason DragModifier grew a MetaShift
        // enumerator: without one the view default canonicalises away to
        // plain Shift, which is a DIFFERENT chord the user never asked for.
        const QVariantMap viewTrigger = wheelViewTriggers->defaultValue.toList().first().toMap();
        QCOMPARE(viewTrigger.value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::MetaShift));

        // The wheel groups take a STRICTER validator than the drag lists:
        // AlwaysActive is a drag-only sentinel, and under the exact matcher
        // these lists are read with it inverts into "match only when nothing
        // is held", which would swallow every unmodified wheel event over the
        // strip. Both wheel keys must drop it; the drag list must keep it.
        QVariantMap sentinel;
        sentinel[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::AlwaysActive);
        sentinel[ConfigDefaults::triggerMouseButtonField()] = 0;
        const QVariantList sentinelList{sentinel};
        QVERIFY(wheelFocusTriggers->validator(sentinelList).toList().isEmpty());
        QVERIFY(wheelViewTriggers->validator(sentinelList).toList().isEmpty());
        const auto* dragInsert =
            findKey(schema, ConfigDefaults::scrollingBehaviorGroup(), ConfigDefaults::triggersKey());
        QVERIFY(dragInsert && dragInsert->validator);
        QCOMPARE(dragInsert->validator(sentinelList).toList().size(), 1);
        // A real chord still survives the wheel validator untouched, so the
        // sentinel drop is not a blanket rejection.
        QVariantMap real;
        real[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::CtrlMeta);
        real[ConfigDefaults::triggerMouseButtonField()] = 0;
        QCOMPARE(wheelFocusTriggers->validator(QVariantList{real}).toList().size(), 1);

        const auto* alwaysCenter = findKey(schema, group, ConfigDefaults::alwaysCenterSingleColumnKey());
        QVERIFY(alwaysCenter);
        QCOMPARE(alwaysCenter->defaultValue.toBool(), ConfigDefaults::scrollingAlwaysCenterSingleColumn());
        const auto* cropStraddlers = findKey(schema, group, ConfigDefaults::cropStraddlersKey());
        QVERIFY(cropStraddlers);
        QCOMPARE(cropStraddlers->defaultValue.toBool(), ConfigDefaults::scrollingCropStraddlers());
        const auto* enabled = findKey(schema, group, ConfigDefaults::enabledKey());
        QVERIFY(enabled);
        QCOMPARE(enabled->defaultValue.toBool(), ConfigDefaults::scrollingEnabled());
    }

    /// Scrolling.Behavior schema guards: the sticky enum falls back to its
    /// default (validIntOr, matching the file's enum convention), the three
    /// step percents clamp into their declared range (clampInt — a
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
        // The view-scroll step shares the sizing pair's range but not its
        // default, so the default is checked against its OWN accessor.
        const auto* viewStep = findKey(schema, group, ConfigDefaults::viewScrollStepPercentKey());
        QVERIFY(viewStep && viewStep->validator);
        QCOMPARE(viewStep->validator(0).toInt(), ConfigDefaults::scrollingStepPercentMin());
        QCOMPARE(viewStep->validator(999).toInt(), ConfigDefaults::scrollingStepPercentMax());
        QCOMPARE(viewStep->defaultValue.toInt(), ConfigDefaults::scrollingViewScrollStepPercent());

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
        const auto* keepAbove = findKey(schema, group, ConfigDefaults::keepFloatingAboveKey());
        QVERIFY(keepAbove);
        QCOMPARE(keepAbove->defaultValue.toBool(), ConfigDefaults::scrollingKeepFloatingAbove());

        // The drag-insert pair. Every other key in the group is pinned
        // above, and these two were the only ones that were not.
        const auto* toggle = findKey(schema, group, ConfigDefaults::toggleActivationKey());
        QVERIFY(toggle);
        QCOMPARE(toggle->defaultValue.toBool(), ConfigDefaults::scrollingDragInsertToggle());

        const auto* triggers = findKey(schema, group, ConfigDefaults::triggersKey());
        QVERIFY(triggers && triggers->validator);
        QCOMPARE(triggers->defaultValue, ConfigDefaults::scrollingDragInsertTriggers());
        // canonicalTriggerList is the shared validator the snapping and
        // tiling trigger lists use, and its two load-bearing behaviours are
        // dropping malformed entries rather than coercing them (a string
        // element must not become a phantom {0,0} trigger that matches every
        // bare drag) and capping the list length. Both asserted here because
        // this group's wiring of it was untested, so a plain passthrough
        // would have shipped unnoticed.
        QVariantList mixed;
        mixed.append(QStringLiteral("garbage"));
        QVariantMap real;
        real[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::CtrlAlt);
        real[ConfigDefaults::triggerMouseButtonField()] = 1;
        mixed.append(real);
        // Range-checked too, not only type-checked: a numeric modifier
        // outside the DragModifier closed set (or a button mask with
        // impossible bits) is dropped like a malformed type, because a
        // trigger no event can ever match must not persist.
        QVariantMap outOfRange;
        outOfRange[ConfigDefaults::triggerModifierField()] = 42;
        outOfRange[ConfigDefaults::triggerMouseButtonField()] = 1;
        mixed.append(outOfRange);
        // The BUTTON arm independently: -1 has every bit set, including the
        // sign bit outside Qt::AllButtons, and would otherwise persist as a
        // trigger that matches every mouse button.
        QVariantMap badButton;
        badButton[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::Shift);
        badButton[ConfigDefaults::triggerMouseButtonField()] = -1;
        mixed.append(badButton);
        const QVariantList canon = triggers->validator(mixed).toList();
        QCOMPARE(canon.size(), 1);
        QCOMPARE(canon.at(0).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::CtrlAlt));
        QCOMPARE(canon.at(0).toMap().value(ConfigDefaults::triggerMouseButtonField()).toInt(), 1);
    }

    /// A factory reset must ANNOUNCE the values it restored.
    ///
    /// reset() clears the groups and reloads, and load() re-emits a property's
    /// NOTIFY by snapshotting before the reload and diffing after. But reset()
    /// deletes the groups BEFORE calling load(), so by the time that snapshot
    /// is taken the store already answers with defaults — the diff sees no
    /// change and fires nothing. Every QML binding then keeps painting the old
    /// value until the app is restarted, which is exactly what a user reports
    /// as "reset did nothing".
    ///
    /// Written against the tab-indicator keys because that is where it was
    /// found, but the defect is general: nothing about it is scrolling-specific.
    void resetAnnouncesRestoredValues()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;

        // Move two properties of different types off their defaults.
        const int customGap = ConfigDefaults::scrollingTabIndicatorGap() + 11;
        settings.setScrollingTabIndicatorGap(customGap);
        settings.setScrollingTabIndicatorPlaceWithinColumn(!ConfigDefaults::scrollingTabIndicatorPlaceWithinColumn());
        QCOMPARE(settings.scrollingTabIndicatorGap(), customGap);

        QSignalSpy gapSpy(&settings, &Settings::scrollingTabIndicatorGapChanged);
        QSignalSpy withinSpy(&settings, &Settings::scrollingTabIndicatorPlaceWithinColumnChanged);
        QVERIFY(gapSpy.isValid() && withinSpy.isValid());

        QVERIFY(settings.reset());

        // The store really did go back to defaults...
        QCOMPARE(settings.scrollingTabIndicatorGap(), ConfigDefaults::scrollingTabIndicatorGap());
        QCOMPARE(settings.scrollingTabIndicatorPlaceWithinColumn(),
                 ConfigDefaults::scrollingTabIndicatorPlaceWithinColumn());
        // ...and said so, which is the half that was missing.
        QCOMPARE(gapSpy.count(), 1);
        QCOMPARE(withinSpy.count(), 1);
    }

    /// The five tab-label font keys follow the standard emit-once contract,
    /// survive a save/reload round trip, and — the one thing peculiar to this
    /// family — announce an EXPLICIT EMPTY family.
    ///
    /// The page's reset button writes the empty string to walk the labels back
    /// to the system font. Empty is ALSO the shipped default, so a setter that
    /// treated "the value equals the default" as "nothing to say" would leave
    /// the page showing the old family over a store that had already forgotten
    /// it. That is why the empty write below is driven from a NON-default
    /// family rather than from a fresh config.
    void tabIndicatorFontSettersEmitOnceAndPersist()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;

        // A fresh config answers the shipped defaults.
        QCOMPARE(settings.scrollingTabIndicatorFontFamily(), ConfigDefaults::scrollingTabIndicatorFontFamily());
        QCOMPARE(settings.scrollingTabIndicatorFontWeight(), ConfigDefaults::scrollingTabIndicatorFontWeight());
        QCOMPARE(settings.scrollingTabIndicatorFontItalic(), ConfigDefaults::scrollingTabIndicatorFontItalic());
        QCOMPARE(settings.scrollingTabIndicatorFontUnderline(), ConfigDefaults::scrollingTabIndicatorFontUnderline());
        QCOMPARE(settings.scrollingTabIndicatorFontStrikeout(), ConfigDefaults::scrollingTabIndicatorFontStrikeout());

        QSignalSpy changedSpy(&settings, &Settings::settingsChanged);
        QSignalSpy familySpy(&settings, &Settings::scrollingTabIndicatorFontFamilyChanged);
        QVERIFY(changedSpy.isValid() && familySpy.isValid());

        // The family carries only the canonicalising validator pinned above
        // (trim, whitespace-only to empty, over-cap to empty): no font
        // NAME is rejected, and the lookup itself happens in the painter.
        // The value used here needs no canonicalisation, so it reaches
        // storage verbatim.
        // Spelled as a family that is deliberately not installed anywhere, so
        // the test pins the storage boundary rather than this machine's font
        // database (and so it does not depend on a CI image having fonts).
        const QString family = QStringLiteral("PlasmaZones Test Family");
        settings.setScrollingTabIndicatorFontFamily(family);
        QCOMPARE(settings.scrollingTabIndicatorFontFamily(), family);
        QCOMPARE(familySpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);
        // Same value again: a full no-op.
        settings.setScrollingTabIndicatorFontFamily(family);
        QCOMPARE(familySpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);

        // The reset path. An EXPLICIT empty family, written from a non-default
        // state, must announce itself. This is what the page's reset button
        // depends on, and what a "skip writes that equal the default" shortcut
        // in the setter would break.
        settings.setScrollingTabIndicatorFontFamily(QString());
        QCOMPARE(settings.scrollingTabIndicatorFontFamily(), QString());
        QCOMPARE(familySpy.count(), 2);
        QCOMPARE(changedSpy.count(), 2);

        QSignalSpy weightSpy(&settings, &Settings::scrollingTabIndicatorFontWeightChanged);
        const int preWeight = changedSpy.count();
        settings.setScrollingTabIndicatorFontWeight(400);
        QCOMPARE(settings.scrollingTabIndicatorFontWeight(), 400);
        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(changedSpy.count(), preWeight + 1);
        // Out of range: the schema clamps on the way in and the setter compares
        // the value the STORE came back with, so the clamped maximum is a real
        // change from 400 and announces itself. A setter comparing the value it
        // was HANDED would leave the page showing 9999 over a stored 900.
        settings.setScrollingTabIndicatorFontWeight(9999);
        QCOMPARE(settings.scrollingTabIndicatorFontWeight(), ConfigDefaults::scrollingTabIndicatorFontWeightMax());
        QCOMPARE(weightSpy.count(), 2);
        QCOMPARE(changedSpy.count(), preWeight + 2);

        // The three style flags, each flipped off its default exactly once,
        // plus one same-value rewrite to pin the no-op half.
        QSignalSpy italicSpy(&settings, &Settings::scrollingTabIndicatorFontItalicChanged);
        QSignalSpy underlineSpy(&settings, &Settings::scrollingTabIndicatorFontUnderlineChanged);
        QSignalSpy strikeoutSpy(&settings, &Settings::scrollingTabIndicatorFontStrikeoutChanged);
        settings.setScrollingTabIndicatorFontItalic(!ConfigDefaults::scrollingTabIndicatorFontItalic());
        settings.setScrollingTabIndicatorFontUnderline(!ConfigDefaults::scrollingTabIndicatorFontUnderline());
        settings.setScrollingTabIndicatorFontStrikeout(!ConfigDefaults::scrollingTabIndicatorFontStrikeout());
        QCOMPARE(italicSpy.count(), 1);
        QCOMPARE(underlineSpy.count(), 1);
        QCOMPARE(strikeoutSpy.count(), 1);
        settings.setScrollingTabIndicatorFontItalic(!ConfigDefaults::scrollingTabIndicatorFontItalic());
        QCOMPARE(italicSpy.count(), 1);

        // Round trip through a real file, with a NON-default family — the only
        // shape sparse persistence actually writes. Deliberately NO assertion
        // about which keys exist on disk: a default-equal value is PRUNED by
        // design, so pinning key presence would pin the opposite of the
        // storage contract.
        settings.setScrollingTabIndicatorFontFamily(family);
        QVERIFY(settings.save());

        Settings reloaded;
        QCOMPARE(reloaded.scrollingTabIndicatorFontFamily(), family);
        QCOMPARE(reloaded.scrollingTabIndicatorFontWeight(), ConfigDefaults::scrollingTabIndicatorFontWeightMax());
        QCOMPARE(reloaded.scrollingTabIndicatorFontItalic(), !ConfigDefaults::scrollingTabIndicatorFontItalic());
        QCOMPARE(reloaded.scrollingTabIndicatorFontUnderline(), !ConfigDefaults::scrollingTabIndicatorFontUnderline());
        QCOMPARE(reloaded.scrollingTabIndicatorFontStrikeout(), !ConfigDefaults::scrollingTabIndicatorFontStrikeout());
    }

    /// Flipping the tab-indicator STYLE re-seeds the shared Width key, but
    /// only when the stored thickness is one the user never chose.
    ///
    /// The two styles want wildly different thicknesses out of one key: a bar
    /// is a few pixels of colour, a chip has to hold a title. Without the
    /// re-seed a style flip leaves a 28 px bar (a stripe) or a 4 px chip run
    /// (no readable title), which is what the setting looks broken as. The
    /// preservation half matters just as much: a deliberate thickness must
    /// survive a round trip, or the re-seed becomes a setting that silently
    /// eats the user's number.
    void tabIndicatorStyleReseedsWidthOnlyWhenUntouched()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;

        const int chips = ConfigDefaults::scrollingTabIndicatorStyleChips();
        const int bar = ConfigDefaults::scrollingTabIndicatorStyleBar();
        const int chipsWidth = ConfigDefaults::scrollingTabIndicatorWidthForChips();
        const int barWidth = ConfigDefaults::scrollingTabIndicatorWidthForBar();
        // The whole re-seed rests on these two differing; if they ever
        // converge the mechanism is pointless and this test would pass
        // vacuously.
        QVERIFY(chipsWidth != barWidth);

        // Fresh config: whatever the shipped style is, paired with its own
        // thickness. Deliberately not hardcoding WHICH style ships — that is
        // a product decision this mechanism has no opinion about, and pinning
        // it here would fail the day it moves for reasons unrelated to the
        // re-seed.
        const int shipped = settings.scrollingTabIndicatorStyle();
        const int other = shipped == bar ? chips : bar;
        QCOMPARE(settings.scrollingTabIndicatorWidth(), ConfigDefaults::scrollingTabIndicatorWidthForStyle(shipped));

        // Untouched thickness follows the style across, both ways.
        settings.setScrollingTabIndicatorStyle(other);
        QCOMPARE(settings.scrollingTabIndicatorWidth(), ConfigDefaults::scrollingTabIndicatorWidthForStyle(other));
        settings.setScrollingTabIndicatorStyle(shipped);
        QCOMPARE(settings.scrollingTabIndicatorWidth(), ConfigDefaults::scrollingTabIndicatorWidthForStyle(shipped));

        // A deliberate thickness is preserved across a round trip. Driven with
        // other/shipped rather than bar/chips: the style is currently `shipped`,
        // so writing `bar` explicitly would be a same-value no-op whenever the
        // shipped style IS bar, and that leg would then pass without the
        // preservation rule ever running.
        settings.setScrollingTabIndicatorWidth(40);
        settings.setScrollingTabIndicatorStyle(other);
        QCOMPARE(settings.scrollingTabIndicatorWidth(), 40);
        settings.setScrollingTabIndicatorStyle(shipped);
        QCOMPARE(settings.scrollingTabIndicatorWidth(), 40);

        // The re-seed is hand-written rather than generated, which puts the
        // emit-once contract at risk in a way width alone cannot show: a
        // re-seeding write must fire settingsChanged exactly ONCE even though
        // it touches two keys, and a same-value style write must fire nothing
        // at all. Asserted on counts because a same-value write is invisible in
        // the width — both branches of the re-seed would leave it untouched.
        QSignalSpy styleSpy(&settings, &Settings::scrollingTabIndicatorStyleChanged);
        QSignalSpy widthSpy(&settings, &Settings::scrollingTabIndicatorWidthChanged);
        QSignalSpy changedSpy(&settings, &Settings::settingsChanged);

        settings.setScrollingTabIndicatorStyle(shipped);
        QCOMPARE(styleSpy.count(), 0);
        QCOMPARE(widthSpy.count(), 0);
        QCOMPARE(changedSpy.count(), 0);

        // Re-seeding flip: style and width both move, and the two-key write
        // still announces once.
        settings.setScrollingTabIndicatorWidth(ConfigDefaults::scrollingTabIndicatorWidthForStyle(shipped));
        styleSpy.clear();
        widthSpy.clear();
        changedSpy.clear();
        settings.setScrollingTabIndicatorStyle(other);
        QCOMPARE(settings.scrollingTabIndicatorWidth(), ConfigDefaults::scrollingTabIndicatorWidthForStyle(other));
        QCOMPARE(styleSpy.count(), 1);
        QCOMPARE(widthSpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);

        // Preserving flip: only the style moves, and it still announces once.
        settings.setScrollingTabIndicatorWidth(40);
        styleSpy.clear();
        widthSpy.clear();
        changedSpy.clear();
        settings.setScrollingTabIndicatorStyle(shipped);
        QCOMPARE(settings.scrollingTabIndicatorWidth(), 40);
        QCOMPARE(styleSpy.count(), 1);
        QCOMPARE(widthSpy.count(), 0);
        QCOMPARE(changedSpy.count(), 1);
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
        // 42 is a literal on purpose: it has to be outside the closed set,
        // and spelling it through an accessor would mean naming a value the
        // set deliberately does not contain.
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

        // The view scroll step is the third of the family, with the wheel
        // as its only reader: same in-range write, same clamp, same single
        // announcement each. A write that clamps to the value already held
        // announces nothing, which is the one arm the twins above do not
        // pin (their clamped writes land on a new value).
        QSignalSpy viewStepSpy(&settings, &Settings::scrollingViewScrollStepPercentChanged);
        const int preViewChanged = changedSpy.count();
        settings.setScrollingViewScrollStepPercent(40);
        QCOMPARE(settings.scrollingViewScrollStepPercent(), 40);
        QCOMPARE(viewStepSpy.count(), 1);
        QCOMPARE(changedSpy.count(), preViewChanged + 1);
        settings.setScrollingViewScrollStepPercent(999);
        QCOMPARE(settings.scrollingViewScrollStepPercent(), ConfigDefaults::scrollingStepPercentMax());
        QCOMPARE(viewStepSpy.count(), 2);
        QCOMPARE(changedSpy.count(), preViewChanged + 2);
        settings.setScrollingViewScrollStepPercent(1000); // clamps to the same max: silent
        QCOMPARE(viewStepSpy.count(), 2);
        QCOMPARE(changedSpy.count(), preViewChanged + 2);
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
        // Both ends DERIVED from the same cap the size assertion uses, for
        // the reason spelled out above: a literal "0.2" here is the cap in
        // disguise, and raising the cap would fail this line for a reason
        // that has nothing to do with keep-earliest.
        QCOMPARE(capped.first(), many.first());
        QCOMPARE(capped.last(), many.at(ConfigDefaults::scrollingPresetIndexMax()));
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
    /// pixel range, under Proportion by [ProportionMin, ProportionMax]. Without
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
        // Seeds deliberately DIFFER from the ConfigDefaults values: with
        // default-equal seeds every "value untouched" row would also pass
        // under a bogus unconditional re-seed to the default.
        const qreal seedProp = 0.35;
        const qreal seedPx = 1200.0;
        // Pinned, not just stated. The seeds are literals and the defaults
        // are not, so retuning either default to the seed's value would
        // silently defang every value-preserving row in the table below
        // rather than failing anything. Asserted here so that change is
        // loud and the fix is obvious (pick a different seed).
        QVERIFY2(!qFuzzyCompare(seedProp, defaultProp),
                 "the proportion seed has collided with its default — pick another, or the untouched rows go vacuous");
        QVERIFY2(!qFuzzyCompare(seedPx, seededPx),
                 "the pixel seed has collided with the re-seed value — pick another, or the untouched rows go vacuous");

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
            // SEED (the flip is announced BEFORE any coercion) — moving the
            // kindChanged emit below the coercion fails this on every
            // coercing row.
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
        // schema's clampDouble(ProportionMin, FixedMax) validator runs on the READ
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
        // The accessor follows $XDG_CONFIG_HOME at runtime, which is what the
        // guard redirects, so this is the file Settings itself writes.
        const QString configFile = ConfigDefaults::configFilePath();
        {
            // Materialise a real config file, then hand-edit the pair into it
            // exactly as a user or a shared blob could. Going through the
            // setters is impossible by construction: they are what maintains
            // the invariant this repair exists to restore.
            Settings seed;
            seed.save();
        }
        // Sparse persistence: a fully-default config may save as no file (or
        // an empty one), so start from whatever save() left and stamp the
        // version so the hand-edited pair reads back without a migration pass.
        QJsonObject root;
        QFile file(configFile);
        if (file.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
        }
        root[ConfigDefaults::versionKey()] = ConfigSchemaVersion;

        QJsonObject group = root.value(ConfigDefaults::scrollingGroup()).toObject();
        group[ConfigDefaults::defaultColumnWidthKindKey()] = kind;
        group[ConfigDefaults::defaultColumnWidthValueKey()] = stored;
        root[ConfigDefaults::scrollingGroup()] = group;
        // A fully-default save() may have created neither the file nor its
        // directory (sparse persistence), and a WriteOnly open cannot create
        // missing parents — make the path unconditionally.
        QDir().mkpath(QFileInfo(configFile).absolutePath());
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

// QTEST_MAIN (not GUILESS): several tests construct Settings, whose resolved
// zone-colour GETTERS read QGuiApplication::palette() (resolution is lazy;
// the load path itself no longer touches the palette), and the schema
// assertions compare against palette-derived resolutions.
QTEST_MAIN(TestScrollingSettings)
#include "test_scrolling_settings.moc"
