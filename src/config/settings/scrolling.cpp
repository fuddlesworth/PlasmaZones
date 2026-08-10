// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

namespace PlasmaZones {

// ── Scrolling (PhosphorConfig::Store-backed) ────────────────────────────────
// Scalars live in m_store under Scrolling; the schema validators own
// the enum/list validation (validIntOr / canonicalProportionList). The width
// value's REAL clamp is kind-aware, because the schema's clampDouble alone
// spans both kinds' ranges and so cannot reject a value that is out of range
// for the kind actually in force. Two places apply it: the hand-written value
// setter below, and normalizeScrollingColumnWidthValue for anything that
// reached the store without passing that setter. Both go through the shared
// clamp/reseed pair in settings_detail.h, which the per-monitor width pair in
// settings/perscreen.cpp uses too — one set of thresholds for every repair
// site.

using settings_detail::clampColumnWidthForKind;
using settings_detail::reseedColumnWidthForKind;

// ISettings gives these two scrolling getters a defaulted body returning a
// hardcoded `true`, so a stub or a partial implementer answers without
// reaching a Settings instance. That header cannot call ConfigDefaults (the
// interface layer does not depend on the config layer), so the agreement is
// pinned here, in a TU that sees both. See the note above the two defaults in
// isettings.h.
static_assert(ConfigDefaults::scrollingTabIndicatorEnabled(),
              "ISettings::scrollingTabIndicatorEnabled defaults to true — update it with this default");
static_assert(ConfigDefaults::scrollingRestoreFloatedWindowsOnLogin(),
              "ISettings::scrollingRestoreFloatedWindowsOnLogin defaults to true — update it with this default");
// IScrollSettings (the LGPL engine interface) carries its own defaulted
// getter for crop mode, returning `false` — the safe clamp for every
// implementor that has not heard of the option. Same duplication class,
// same pin, different interface and direction.
static_assert(!ConfigDefaults::scrollingCropStraddlers(),
              "IScrollSettings::scrollingCropStraddlers defaults to false — update it with this default");
// The tab indicator's paint half carries the same interface-side defaults, for
// the same reason: the overlay service reads them through ISettings.
static_assert(ConfigDefaults::scrollingTabIndicatorStyle() == 1,
              "ISettings::scrollingTabIndicatorStyle defaults to 1 (segment bar) — update it with this default");
static_assert(ConfigDefaults::scrollingTabIndicatorGapsBetweenTabs() == 0,
              "ISettings::scrollingTabIndicatorGapsBetweenTabs defaults to 0 — update it with this default");
static_assert(ConfigDefaults::scrollingTabIndicatorCornerRadius() == 0,
              "ISettings::scrollingTabIndicatorCornerRadius defaults to 0 (square) — update it with this default");
// The drop indicator's paint keys, same story: the overlay service reads them
// through ISettings, so a stub answering from the interface body must agree.
// The two COLOUR defaults have no assert here and cannot get one: they return
// a default-constructed QString, which is not a constant expression, and their
// agreement rests on the doc comment in isettings.h. That is the whole
// unasserted set in THIS indicator's family — the opacity joined the checked
// ones when it became constexpr. The three tab-indicator colours are equally
// unasserted for the same non-constexpr reason; test_scrolling_settings.cpp
// pins their SCHEMA defaults (via ConfigDefaults) at runtime, while their
// ISettings-body agreement, like the drop indicator's, rests on the doc
// comment in isettings.h.
static_assert(ConfigDefaults::scrollingDropIndicatorEnabled(),
              "ISettings::scrollingDropIndicatorEnabled defaults to true — update it with this default");
static_assert(ConfigDefaults::scrollingDropIndicatorOpacity() == 0.25,
              "ISettings::scrollingDropIndicatorOpacity defaults to 0.25 — update it with this default");
static_assert(ConfigDefaults::scrollingDropIndicatorBorderWidth() == 2,
              "ISettings::scrollingDropIndicatorBorderWidth defaults to 2 — update it with this default");
static_assert(ConfigDefaults::scrollingDropIndicatorBorderRadius() == 8,
              "ISettings::scrollingDropIndicatorBorderRadius defaults to 8 (the zone overlay's radius) — update it "
              "with this default");

P_STORE_GET(bool, scrollingEnabled, scrollingGroup, enabledKey, bool)
P_STORE_SET_BOOL(setScrollingEnabled, scrollingGroup, enabledKey, scrollingEnabledChanged)

P_STORE_GET(int, scrollingCenterFocusedColumn, scrollingGroup, centerFocusedColumnKey, int)
P_STORE_SET_INT(setScrollingCenterFocusedColumn, scrollingGroup, centerFocusedColumnKey,
                scrollingCenterFocusedColumnChanged)

P_STORE_GET(bool, scrollingAlwaysCenterSingleColumn, scrollingGroup, alwaysCenterSingleColumnKey, bool)
P_STORE_SET_BOOL(setScrollingAlwaysCenterSingleColumn, scrollingGroup, alwaysCenterSingleColumnKey,
                 scrollingAlwaysCenterSingleColumnChanged)

P_STORE_GET(bool, scrollingCropStraddlers, scrollingGroup, cropStraddlersKey, bool)
P_STORE_SET_BOOL(setScrollingCropStraddlers, scrollingGroup, cropStraddlersKey, scrollingCropStraddlersChanged)

P_STORE_GET(int, scrollingDefaultColumnWidthKind, scrollingGroup, defaultColumnWidthKindKey, int)

// Hand-written kind setter: the shared value key serves two kinds under one
// schema clamp, so a kind flip must coerce the stored value into the new
// kind's range — otherwise Fixed→Proportion leaves 800 stored (engine clamps
// to 100%) or Proportion→Fixed leaves 0.5 stored (the engine's qMax(1, …)
// backstop renders it one pixel wide) while the page displays something
// else entirely.
void Settings::setScrollingDefaultColumnWidthKind(int value)
{
    const int before =
        m_store->read<int>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey());
    m_store->write(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey(), value);
    const int after = m_store->read<int>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey());
    if (after == before) {
        return;
    }
    // Both arms are preservation tests, not kind sniffs: each re-seeds only
    // when the stored value cannot belong to the kind being entered. Fixed
    // re-seeds below the pixel floor, Proportion above 1.0. A legitimate
    // value of either kind parked through a ClientDecides hop therefore
    // survives the round trip. A hand-edited config with an inconsistent pair
    // (kind=Fixed, value=0.5) never reaches this setter at all; that case is
    // caught once by normalizeScrollingColumnWidthValue at load.
    // The kind flip is announced FIRST: a QML handler keyed on the kind
    // NOTIFY must observe the new kind before (not after) the value
    // coercion and the engine refresh it triggers.
    Q_EMIT scrollingDefaultColumnWidthKindChanged();
    const qreal stored =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    const bool isFixed = after == ConfigDefaults::scrollingWidthKindFixed();
    const bool isProportion = after == ConfigDefaults::scrollingWidthKindProportion();
    // No before/after kind comparison is needed inside the arms: the
    // early-return above guarantees after != before, so entering Fixed
    // implies the previous kind was not Fixed.
    if (isFixed && stored < ConfigDefaults::scrollingDefaultColumnWidthFixedMin()) {
        // Entering Fixed with something that is not a plausible pixel width —
        // a proportion arriving straight from Proportion, or one parked
        // through a ClientDecides hop. Seed a sane pixel width. A pixel count
        // already sitting there (the user's retained width across a
        // Fixed→ClientDecides→Fixed round trip) is left alone.
        setScrollingDefaultColumnWidthValue(ConfigDefaults::scrollingDefaultColumnWidthFixedPx());
    } else if (isProportion && stored > ConfigDefaults::scrollingDefaultColumnWidthProportionMax()) {
        // Entering Proportion with pixels stored — whether directly from
        // Fixed or via a ClientDecides hop (ClientDecides ignores the value
        // and deliberately leaves it untouched, so pixels can arrive here
        // two transitions later). A pixel count fed to the engine's
        // qBound(0.05, …, 1.0) would open every column at 100% width.
        setScrollingDefaultColumnWidthValue(ConfigDefaults::scrollingDefaultColumnWidthValue());
    }
    // One aggregate emit per kind flip: when the value coercion above ran,
    // its nested setter already emitted settingsChanged — a second emit here
    // would run the engine's refresh+retile sweep twice for one user action.
    const qreal storedNow =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    if (qFuzzyCompare(1.0 + storedNow, 1.0 + stored)) {
        Q_EMIT settingsChanged();
    }
}

P_STORE_GET(qreal, scrollingDefaultColumnWidthValue, scrollingGroup, defaultColumnWidthValueKey, double)

// Post-load repair for the shared width VALUE key. The key serves both kinds,
// so the schema's clampDouble has to span their union (0.05 proportion up to
// 10000 px) and cannot reject a Fixed=5px that reached the store without
// passing the setter below.
//
// SCOPE: called from Settings::load, so it catches whatever the reparse
// brought in — a hand-edited config, a config import, the Discard reload —
// from applyConfigOverlayStaged, which writes the store through importFromJson
// without a load(), and from the per-page discardKeys/resetKeys, which write
// the store key by key from a page manifest. Profile staging needs it just as
// much: a shared blob carrying kind=Fixed with value 0.5 would otherwise make
// the engine open every column ONE PIXEL wide for the whole session.
//
// Read-time coercion is deliberately NOT how this is done: the kind setter
// announces the flip BEFORE coercing the value, and a clamping getter would
// report the new kind's bounds against the old kind's value in that window,
// breaking the emit ordering a test pins.
void Settings::normalizeScrollingColumnWidthValue()
{
    const int kind = scrollingDefaultColumnWidthKind();
    const qreal stored = scrollingDefaultColumnWidthValue();
    // ClientDecides and Preset store no width of their own — both
    // deliberately leave whatever the previous kind wrote in place (Preset
    // resolves through its index key), so there is nothing to validate
    // against. reseedColumnWidthForKind returns those two untouched, which is
    // why the early return that used to sit here is gone: the kind dispatch
    // belongs in the shared helper, where the per-monitor repair path gets it
    // for free.
    const qreal coerced = reseedColumnWidthForKind(stored, kind);
    if (qFuzzyCompare(1.0 + stored, 1.0 + coerced)) {
        return;
    }
    // "In memory for this session": load() runs this BEFORE captureBaseline,
    // so the repaired value is baked into the committed baseline, never
    // reads as a pending edit, and only reaches disk when some unrelated
    // change makes the user save. Each launch re-repairs the same stored
    // pair; the engine never sees the bad value, so this is deliberate.
    qCWarning(lcConfig) << "scrolling: stored column width" << stored << "is out of range for the current kind" << kind
                        << "— repaired to" << coerced << "in memory for this session";
    m_store->write(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey(), coerced);
    // NO Q_EMIT here. EVERY caller — load(), applyConfigOverlayStaged, and the
    // per-page discardKeys/resetKeys —
    // snapshots every Q_PROPERTY before mutating the store and re-emits each
    // changed NOTIFY after this returns. Emitting here would double-fire on
    // a coercing load or a coercing staged apply, and
    // fire spuriously on a Discard reload where the in-memory value was
    // already coerced (disk still holds the bad pair, so this coerces again,
    // but the property never changed from any consumer's point of view).
}

// Hand-written value setter: kind-aware clamp (Proportion values live in
// [ProportionMin, ProportionMax]; Fixed in pixels with a FixedMin floor, rounded
// to whole pixels by the engine on load) — the schema clamp alone spans
// both ranges. Under ClientDecides and Preset the clamp is an identity, so a
// D-Bus write while one of those kinds is in force cannot collapse the pixel
// width a later Fixed round trip is meant to get back.
void Settings::setScrollingDefaultColumnWidthValue(qreal value)
{
    value = clampColumnWidthForKind(value, scrollingDefaultColumnWidthKind());
    const qreal before =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    m_store->write(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey(), value);
    const qreal after =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    if (qFuzzyCompare(1.0 + before, 1.0 + after)) {
        return;
    }
    Q_EMIT scrollingDefaultColumnWidthValueChanged();
    Q_EMIT settingsChanged();
}

P_STORE_GET(int, scrollingDefaultColumnDisplay, scrollingGroup, defaultColumnDisplayKey, int)
P_STORE_SET_INT(setScrollingDefaultColumnDisplay, scrollingGroup, defaultColumnDisplayKey,
                scrollingDefaultColumnDisplayChanged)

P_STORE_GET(int, scrollingDefaultColumnWidthPresetIndex, scrollingGroup, defaultColumnWidthPresetIndexKey, int)
P_STORE_SET_INT(setScrollingDefaultColumnWidthPresetIndex, scrollingGroup, defaultColumnWidthPresetIndexKey,
                scrollingDefaultColumnWidthPresetIndexChanged)

// Height trio: unlike the width pair, the value key serves one kind (Fixed)
// so the schema clamp is the whole story and the plain macros suffice.
P_STORE_GET(int, scrollingDefaultWindowHeightKind, scrollingGroup, defaultWindowHeightKindKey, int)
P_STORE_SET_INT(setScrollingDefaultWindowHeightKind, scrollingGroup, defaultWindowHeightKindKey,
                scrollingDefaultWindowHeightKindChanged)
P_STORE_GET(qreal, scrollingDefaultWindowHeightValue, scrollingGroup, defaultWindowHeightValueKey, double)
P_STORE_SET_DOUBLE(setScrollingDefaultWindowHeightValue, scrollingGroup, defaultWindowHeightValueKey,
                   scrollingDefaultWindowHeightValueChanged)
P_STORE_GET(int, scrollingDefaultWindowHeightPresetIndex, scrollingGroup, defaultWindowHeightPresetIndexKey, int)
P_STORE_SET_INT(setScrollingDefaultWindowHeightPresetIndex, scrollingGroup, defaultWindowHeightPresetIndexKey,
                scrollingDefaultWindowHeightPresetIndexChanged)

// Preset lists: comma-joined QString on disk, QStringList through
// IScrollSettings (the engine parses the decimals), raw string for QML.
QStringList Settings::scrollingPresetColumnWidths() const
{
    return settings_detail::parseCommaList(
        m_store->read<QString>(ConfigDefaults::scrollingGroup(), ConfigDefaults::presetColumnWidthsKey()));
}

P_STORE_GET(QString, scrollingPresetColumnWidthsString, scrollingGroup, presetColumnWidthsKey, QString)
P_STORE_SET_STRING(setScrollingPresetColumnWidths, scrollingGroup, presetColumnWidthsKey,
                   scrollingPresetColumnWidthsChanged)

QStringList Settings::scrollingPresetWindowHeights() const
{
    return settings_detail::parseCommaList(
        m_store->read<QString>(ConfigDefaults::scrollingGroup(), ConfigDefaults::presetWindowHeightsKey()));
}

P_STORE_GET(QString, scrollingPresetWindowHeightsString, scrollingGroup, presetWindowHeightsKey, QString)
P_STORE_SET_STRING(setScrollingPresetWindowHeights, scrollingGroup, presetWindowHeightsKey,
                   scrollingPresetWindowHeightsChanged)

// The template an unassigned screen resolves to, empty for none.
P_STORE_GET(QString, defaultScrollingTemplate, scrollingGroup, defaultTemplateKey, QString)
P_STORE_SET_STRING(setDefaultScrollingTemplate, scrollingGroup, defaultTemplateKey, defaultScrollingTemplateChanged)

// View knobs, on the Scrolling group with the sizing defaults above rather
// than on Scrolling.Behavior: they describe how the strip is drawn, not how
// windows are handled.
P_STORE_GET(bool, scrollingWheelFocusEnabled, scrollingGroup, wheelFocusEnabledKey, bool)
P_STORE_SET_BOOL(setScrollingWheelFocusEnabled, scrollingGroup, wheelFocusEnabledKey, scrollingWheelFocusEnabledChanged)

P_STORE_GET(bool, scrollingWheelFocusInverted, scrollingGroup, wheelFocusInvertedKey, bool)
P_STORE_SET_BOOL(setScrollingWheelFocusInverted, scrollingGroup, wheelFocusInvertedKey,
                 scrollingWheelFocusInvertedChanged)

// ── Scrolling tab indicator (Scrolling.TabIndicator) ────────────────────────
// Its own group rather than more Tab*-prefixed leaves on Scrolling, so the
// page reset manifest and the rule slots address one subtree. The schema
// validators own the enum closed sets (validIntOr) and the numeric clamps; the
// colours are free-form strings whose EMPTY value means "follow the theme", so
// they carry canonicalThemeFallbackColor (empty or QColor-parseable) rather
// than a closed set — the schema validator is the ONLY guard on the
// hand-edited-config path, where an unsanitized string would reach QML as an
// invalid QColor and paint black.

P_STORE_GET(bool, scrollingTabIndicatorEnabled, scrollingTabIndicatorGroup, enabledKey, bool)
P_STORE_SET_BOOL(setScrollingTabIndicatorEnabled, scrollingTabIndicatorGroup, enabledKey,
                 scrollingTabIndicatorEnabledChanged)

P_STORE_GET(int, scrollingTabIndicatorStyle, scrollingTabIndicatorGroup, tabIndicatorStyleKey, int)

// Hand-written style setter, the setScrollingDefaultColumnWidthKind shape: one
// stored Width key serves both styles, and the thickness that suits one is
// unusable for the other. A bar is a few pixels of colour; a chip has to hold a
// title. Flipping style without re-seeding leaves a 28 px bar (a stripe) or a
// 4 px chip run (no readable title at all), which is what the setting looks
// broken as.
//
// PRESERVATION TEST, not a style sniff, exactly like the width-kind setter's
// two arms: re-seed ONLY when the stored thickness is the value the OTHER
// style would have been given, i.e. a thickness the user demonstrably never
// chose. Any other number is a deliberate choice and survives the flip, so a
// user who set 40 for chips still has 40 after a bar round trip.
void Settings::setScrollingTabIndicatorStyle(int style)
{
    const int before =
        m_store->read<int>(ConfigDefaults::scrollingTabIndicatorGroup(), ConfigDefaults::tabIndicatorStyleKey());
    m_store->write(ConfigDefaults::scrollingTabIndicatorGroup(), ConfigDefaults::tabIndicatorStyleKey(), style);
    const int after =
        m_store->read<int>(ConfigDefaults::scrollingTabIndicatorGroup(), ConfigDefaults::tabIndicatorStyleKey());
    if (after == before) {
        return;
    }
    // Style announced FIRST, for the width-kind setter's reason: a QML handler
    // keyed on the style NOTIFY must observe the new style before the width
    // re-seed and the engine refresh it triggers.
    Q_EMIT scrollingTabIndicatorStyleChanged();

    // ONE aggregate emit per style flip, the setScrollingDefaultColumnWidthKind
    // contract. The nested width setter is generated by P_STORE_SET_INT, whose
    // body ends `Q_EMIT signal(); Q_EMIT settingsChanged();` — so emitting here
    // unconditionally would fire the aggregate TWICE for one user action, and
    // the first of the pair would be observed with the new style paired with
    // the STALE width, driving the engine's refresh+retile sweep twice.
    const int storedWidth =
        m_store->read<int>(ConfigDefaults::scrollingTabIndicatorGroup(), ConfigDefaults::widthKey());
    if (storedWidth == ConfigDefaults::scrollingTabIndicatorWidthForStyle(before)) {
        setScrollingTabIndicatorWidth(ConfigDefaults::scrollingTabIndicatorWidthForStyle(after));
    } else {
        Q_EMIT settingsChanged();
    }
}

P_STORE_GET(int, scrollingTabIndicatorPosition, scrollingTabIndicatorGroup, positionKey, int)
P_STORE_SET_INT(setScrollingTabIndicatorPosition, scrollingTabIndicatorGroup, positionKey,
                scrollingTabIndicatorPositionChanged)

P_STORE_GET(bool, scrollingTabIndicatorHideWhenSingleTab, scrollingTabIndicatorGroup, hideWhenSingleTabKey, bool)
P_STORE_SET_BOOL(setScrollingTabIndicatorHideWhenSingleTab, scrollingTabIndicatorGroup, hideWhenSingleTabKey,
                 scrollingTabIndicatorHideWhenSingleTabChanged)

P_STORE_GET(bool, scrollingTabIndicatorPlaceWithinColumn, scrollingTabIndicatorGroup, placeWithinColumnKey, bool)
P_STORE_SET_BOOL(setScrollingTabIndicatorPlaceWithinColumn, scrollingTabIndicatorGroup, placeWithinColumnKey,
                 scrollingTabIndicatorPlaceWithinColumnChanged)

P_STORE_GET(int, scrollingTabIndicatorGap, scrollingTabIndicatorGroup, gapKey, int)
P_STORE_SET_INT(setScrollingTabIndicatorGap, scrollingTabIndicatorGroup, gapKey, scrollingTabIndicatorGapChanged)

P_STORE_GET(int, scrollingTabIndicatorWidth, scrollingTabIndicatorGroup, widthKey, int)
P_STORE_SET_INT(setScrollingTabIndicatorWidth, scrollingTabIndicatorGroup, widthKey, scrollingTabIndicatorWidthChanged)

P_STORE_GET(qreal, scrollingTabIndicatorLengthProportion, scrollingTabIndicatorGroup, lengthProportionKey, double)
P_STORE_SET_DOUBLE(setScrollingTabIndicatorLengthProportion, scrollingTabIndicatorGroup, lengthProportionKey,
                   scrollingTabIndicatorLengthProportionChanged)

P_STORE_GET(int, scrollingTabIndicatorGapsBetweenTabs, scrollingTabIndicatorGroup, gapsBetweenTabsKey, int)
P_STORE_SET_INT(setScrollingTabIndicatorGapsBetweenTabs, scrollingTabIndicatorGroup, gapsBetweenTabsKey,
                scrollingTabIndicatorGapsBetweenTabsChanged)

P_STORE_GET(int, scrollingTabIndicatorCornerRadius, scrollingTabIndicatorGroup, cornerRadiusKey, int)
P_STORE_SET_INT(setScrollingTabIndicatorCornerRadius, scrollingTabIndicatorGroup, cornerRadiusKey,
                scrollingTabIndicatorCornerRadiusChanged)

P_STORE_GET(QString, scrollingTabIndicatorActiveColor, scrollingTabIndicatorGroup, activeColorKey, QString)
P_STORE_SET_STRING(setScrollingTabIndicatorActiveColor, scrollingTabIndicatorGroup, activeColorKey,
                   scrollingTabIndicatorActiveColorChanged)

P_STORE_GET(QString, scrollingTabIndicatorInactiveColor, scrollingTabIndicatorGroup, inactiveColorKey, QString)
P_STORE_SET_STRING(setScrollingTabIndicatorInactiveColor, scrollingTabIndicatorGroup, inactiveColorKey,
                   scrollingTabIndicatorInactiveColorChanged)

P_STORE_GET(QString, scrollingTabIndicatorUrgentColor, scrollingTabIndicatorGroup, urgentColorKey, QString)
P_STORE_SET_STRING(setScrollingTabIndicatorUrgentColor, scrollingTabIndicatorGroup, urgentColorKey,
                   scrollingTabIndicatorUrgentColorChanged)

// ── Scrolling drop indicator (Scrolling.DropIndicator) ──────────────────────
// The drop-target highlight painted during a drag re-insert. Paint-only: the
// engine never reads these, it resolves the indicator's rect from the same
// layout math the drop uses. Like the tab colours above, the colour is a
// free-form string whose EMPTY value means "follow the theme", so it carries
// canonicalThemeFallbackColor rather than a closed set (the disk path's only
// guard against a black-painting unparseable string).

P_STORE_GET(bool, scrollingDropIndicatorEnabled, scrollingDropIndicatorGroup, enabledKey, bool)
P_STORE_SET_BOOL(setScrollingDropIndicatorEnabled, scrollingDropIndicatorGroup, enabledKey,
                 scrollingDropIndicatorEnabledChanged)

P_STORE_GET(QString, scrollingDropIndicatorColor, scrollingDropIndicatorGroup, colorKey, QString)
P_STORE_SET_STRING(setScrollingDropIndicatorColor, scrollingDropIndicatorGroup, colorKey,
                   scrollingDropIndicatorColorChanged)

P_STORE_GET(QString, scrollingDropIndicatorBorderColor, scrollingDropIndicatorGroup, borderColorKey, QString)
P_STORE_SET_STRING(setScrollingDropIndicatorBorderColor, scrollingDropIndicatorGroup, borderColorKey,
                   scrollingDropIndicatorBorderColorChanged)

// `double`, not the `qreal` every other floating getter in this file spells,
// because the type has to match the ISettings virtual it overrides and that
// one is declared double. The two are the same type on every platform this
// builds for; the spelling difference is the interface's, not this file's.
P_STORE_GET(double, scrollingDropIndicatorOpacity, scrollingDropIndicatorGroup, opacityKey, double)
P_STORE_SET_DOUBLE(setScrollingDropIndicatorOpacity, scrollingDropIndicatorGroup, opacityKey,
                   scrollingDropIndicatorOpacityChanged)

P_STORE_GET(int, scrollingDropIndicatorBorderWidth, scrollingDropIndicatorGroup, widthKey, int)
P_STORE_SET_INT(setScrollingDropIndicatorBorderWidth, scrollingDropIndicatorGroup, widthKey,
                scrollingDropIndicatorBorderWidthChanged)

P_STORE_GET(int, scrollingDropIndicatorBorderRadius, scrollingDropIndicatorGroup, radiusKey, int)
P_STORE_SET_INT(setScrollingDropIndicatorBorderRadius, scrollingDropIndicatorGroup, radiusKey,
                scrollingDropIndicatorBorderRadiusChanged)

// ── Scrolling behavior (Scrolling.Behavior) ─────────────────────────────────
// Shared leaf key names under the scrolling behavior group; the schema
// validators own enum validation (validIntOr snaps a bad sticky value back
// to the default on read, like every other stored enum) and range clamping
// (clampInt on the step percents).

// ── Scrolling drag-insert triggers (PhosphorConfig::Store-backed) ───────────
// Hand-written like the autotile pair in triggers.cpp: trigger lists are
// QVariantList payloads, outside the P_STORE macro vocabulary.

QVariantList Settings::scrollingDragInsertTriggers() const
{
    return m_store->readVariant(ConfigDefaults::scrollingBehaviorGroup(), ConfigDefaults::triggersKey()).toList();
}
void Settings::setScrollingDragInsertTriggers(const QVariantList& triggers)
{
    writeTriggerList(ConfigDefaults::scrollingBehaviorGroup(), ConfigDefaults::triggersKey(), triggers,
                     &Settings::scrollingDragInsertTriggersChanged);
}

P_STORE_GET(bool, scrollingDragInsertToggle, scrollingBehaviorGroup, toggleActivationKey, bool)
P_STORE_SET_BOOL(setScrollingDragInsertToggle, scrollingBehaviorGroup, toggleActivationKey,
                 scrollingDragInsertToggleChanged)

P_STORE_GET(int, scrollingInsertPosition, scrollingBehaviorGroup, insertPositionKey, int)
P_STORE_SET_INT(setScrollingInsertPosition, scrollingBehaviorGroup, insertPositionKey, scrollingInsertPositionChanged)

P_STORE_GET(bool, scrollingFocusNewWindows, scrollingBehaviorGroup, focusNewWindowsKey, bool)
P_STORE_SET_BOOL(setScrollingFocusNewWindows, scrollingBehaviorGroup, focusNewWindowsKey,
                 scrollingFocusNewWindowsChanged)

P_STORE_GET(bool, scrollingFocusFollowsMouse, scrollingBehaviorGroup, focusFollowsMouseKey, bool)
P_STORE_SET_BOOL(setScrollingFocusFollowsMouse, scrollingBehaviorGroup, focusFollowsMouseKey,
                 scrollingFocusFollowsMouseChanged)

P_STORE_GET(int, scrollingStickyWindowHandling, scrollingBehaviorGroup, stickyWindowHandlingKey, int)
P_STORE_SET_INT(setScrollingStickyWindowHandling, scrollingBehaviorGroup, stickyWindowHandlingKey,
                scrollingStickyWindowHandlingChanged)

P_STORE_GET(bool, scrollingRespectMinimumSize, scrollingBehaviorGroup, respectMinimumSizeKey, bool)
P_STORE_SET_BOOL(setScrollingRespectMinimumSize, scrollingBehaviorGroup, respectMinimumSizeKey,
                 scrollingRespectMinimumSizeChanged)

P_STORE_GET(bool, scrollingRestoreStripsOnLogin, scrollingBehaviorGroup, restoreOnLoginKey, bool)
P_STORE_SET_BOOL(setScrollingRestoreStripsOnLogin, scrollingBehaviorGroup, restoreOnLoginKey,
                 scrollingRestoreStripsOnLoginChanged)

P_STORE_GET(bool, scrollingRestoreFloatedWindowsOnLogin, scrollingBehaviorGroup, restoreFloatedOnLoginKey, bool)
P_STORE_SET_BOOL(setScrollingRestoreFloatedWindowsOnLogin, scrollingBehaviorGroup, restoreFloatedOnLoginKey,
                 scrollingRestoreFloatedWindowsOnLoginChanged)

P_STORE_GET(int, scrollingColumnWidthStepPercent, scrollingBehaviorGroup, columnWidthStepPercentKey, int)
P_STORE_SET_INT(setScrollingColumnWidthStepPercent, scrollingBehaviorGroup, columnWidthStepPercentKey,
                scrollingColumnWidthStepPercentChanged)

P_STORE_GET(int, scrollingWindowHeightStepPercent, scrollingBehaviorGroup, windowHeightStepPercentKey, int)
P_STORE_SET_INT(setScrollingWindowHeightStepPercent, scrollingBehaviorGroup, windowHeightStepPercentKey,
                scrollingWindowHeightStepPercentChanged)

// ── Scrolling shortcuts ─────────────────────────────────────────────────────

P_STORE_GET(QString, scrollingFocusColumnFirstShortcut, shortcutsScrollingGroup, focusColumnFirstKey, QString)
P_STORE_SET_STRING(setScrollingFocusColumnFirstShortcut, shortcutsScrollingGroup, focusColumnFirstKey,
                   scrollingFocusColumnFirstShortcutChanged)
P_STORE_GET(QString, scrollingFocusColumnLastShortcut, shortcutsScrollingGroup, focusColumnLastKey, QString)
P_STORE_SET_STRING(setScrollingFocusColumnLastShortcut, shortcutsScrollingGroup, focusColumnLastKey,
                   scrollingFocusColumnLastShortcutChanged)
P_STORE_GET(QString, scrollingMoveColumnToFirstShortcut, shortcutsScrollingGroup, moveColumnToFirstKey, QString)
P_STORE_SET_STRING(setScrollingMoveColumnToFirstShortcut, shortcutsScrollingGroup, moveColumnToFirstKey,
                   scrollingMoveColumnToFirstShortcutChanged)
P_STORE_GET(QString, scrollingMoveColumnToLastShortcut, shortcutsScrollingGroup, moveColumnToLastKey, QString)
P_STORE_SET_STRING(setScrollingMoveColumnToLastShortcut, shortcutsScrollingGroup, moveColumnToLastKey,
                   scrollingMoveColumnToLastShortcutChanged)
P_STORE_GET(QString, scrollingConsumeWindowShortcut, shortcutsScrollingGroup, consumeWindowKey, QString)
P_STORE_SET_STRING(setScrollingConsumeWindowShortcut, shortcutsScrollingGroup, consumeWindowKey,
                   scrollingConsumeWindowShortcutChanged)
P_STORE_GET(QString, scrollingExpelWindowShortcut, shortcutsScrollingGroup, expelWindowKey, QString)
P_STORE_SET_STRING(setScrollingExpelWindowShortcut, shortcutsScrollingGroup, expelWindowKey,
                   scrollingExpelWindowShortcutChanged)
P_STORE_GET(QString, scrollingConsumeOrExpelLeftShortcut, shortcutsScrollingGroup, consumeOrExpelLeftKey, QString)
P_STORE_SET_STRING(setScrollingConsumeOrExpelLeftShortcut, shortcutsScrollingGroup, consumeOrExpelLeftKey,
                   scrollingConsumeOrExpelLeftShortcutChanged)
P_STORE_GET(QString, scrollingConsumeOrExpelRightShortcut, shortcutsScrollingGroup, consumeOrExpelRightKey, QString)
P_STORE_SET_STRING(setScrollingConsumeOrExpelRightShortcut, shortcutsScrollingGroup, consumeOrExpelRightKey,
                   scrollingConsumeOrExpelRightShortcutChanged)
P_STORE_GET(QString, scrollingCenterColumnShortcut, shortcutsScrollingGroup, centerColumnKey, QString)
P_STORE_SET_STRING(setScrollingCenterColumnShortcut, shortcutsScrollingGroup, centerColumnKey,
                   scrollingCenterColumnShortcutChanged)
P_STORE_GET(QString, scrollingToggleColumnTabbedShortcut, shortcutsScrollingGroup, toggleColumnTabbedKey, QString)
P_STORE_SET_STRING(setScrollingToggleColumnTabbedShortcut, shortcutsScrollingGroup, toggleColumnTabbedKey,
                   scrollingToggleColumnTabbedShortcutChanged)
P_STORE_GET(QString, scrollingToggleWindowedFullscreenShortcut, shortcutsScrollingGroup, toggleWindowedFullscreenKey,
            QString)
P_STORE_SET_STRING(setScrollingToggleWindowedFullscreenShortcut, shortcutsScrollingGroup, toggleWindowedFullscreenKey,
                   scrollingToggleWindowedFullscreenShortcutChanged)
P_STORE_GET(QString, scrollingCycleColumnWidthShortcut, shortcutsScrollingGroup, cycleColumnWidthKey, QString)
P_STORE_SET_STRING(setScrollingCycleColumnWidthShortcut, shortcutsScrollingGroup, cycleColumnWidthKey,
                   scrollingCycleColumnWidthShortcutChanged)
P_STORE_GET(QString, scrollingCycleColumnWidthBackShortcut, shortcutsScrollingGroup, cycleColumnWidthBackKey, QString)
P_STORE_SET_STRING(setScrollingCycleColumnWidthBackShortcut, shortcutsScrollingGroup, cycleColumnWidthBackKey,
                   scrollingCycleColumnWidthBackShortcutChanged)
P_STORE_GET(QString, scrollingIncreaseColumnWidthShortcut, shortcutsScrollingGroup, increaseColumnWidthKey, QString)
P_STORE_SET_STRING(setScrollingIncreaseColumnWidthShortcut, shortcutsScrollingGroup, increaseColumnWidthKey,
                   scrollingIncreaseColumnWidthShortcutChanged)
P_STORE_GET(QString, scrollingDecreaseColumnWidthShortcut, shortcutsScrollingGroup, decreaseColumnWidthKey, QString)
P_STORE_SET_STRING(setScrollingDecreaseColumnWidthShortcut, shortcutsScrollingGroup, decreaseColumnWidthKey,
                   scrollingDecreaseColumnWidthShortcutChanged)
P_STORE_GET(QString, scrollingMaximizeColumnShortcut, shortcutsScrollingGroup, maximizeColumnKey, QString)
P_STORE_SET_STRING(setScrollingMaximizeColumnShortcut, shortcutsScrollingGroup, maximizeColumnKey,
                   scrollingMaximizeColumnShortcutChanged)
P_STORE_GET(QString, scrollingExpandColumnShortcut, shortcutsScrollingGroup, expandColumnKey, QString)
P_STORE_SET_STRING(setScrollingExpandColumnShortcut, shortcutsScrollingGroup, expandColumnKey,
                   scrollingExpandColumnShortcutChanged)
P_STORE_GET(QString, scrollingCycleWindowHeightShortcut, shortcutsScrollingGroup, cycleWindowHeightKey, QString)
P_STORE_SET_STRING(setScrollingCycleWindowHeightShortcut, shortcutsScrollingGroup, cycleWindowHeightKey,
                   scrollingCycleWindowHeightShortcutChanged)
P_STORE_GET(QString, scrollingCycleWindowHeightBackShortcut, shortcutsScrollingGroup, cycleWindowHeightBackKey, QString)
P_STORE_SET_STRING(setScrollingCycleWindowHeightBackShortcut, shortcutsScrollingGroup, cycleWindowHeightBackKey,
                   scrollingCycleWindowHeightBackShortcutChanged)
P_STORE_GET(QString, scrollingIncreaseWindowHeightShortcut, shortcutsScrollingGroup, increaseWindowHeightKey, QString)
P_STORE_SET_STRING(setScrollingIncreaseWindowHeightShortcut, shortcutsScrollingGroup, increaseWindowHeightKey,
                   scrollingIncreaseWindowHeightShortcutChanged)
P_STORE_GET(QString, scrollingDecreaseWindowHeightShortcut, shortcutsScrollingGroup, decreaseWindowHeightKey, QString)
P_STORE_SET_STRING(setScrollingDecreaseWindowHeightShortcut, shortcutsScrollingGroup, decreaseWindowHeightKey,
                   scrollingDecreaseWindowHeightShortcutChanged)
P_STORE_GET(QString, scrollingResetWindowHeightsShortcut, shortcutsScrollingGroup, resetWindowHeightsKey, QString)
P_STORE_SET_STRING(setScrollingResetWindowHeightsShortcut, shortcutsScrollingGroup, resetWindowHeightsKey,
                   scrollingResetWindowHeightsShortcutChanged)

} // namespace PlasmaZones
