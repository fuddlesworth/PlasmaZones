// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

namespace PlasmaZones {

// Enum setters: stored as int, exposed via the enum-typed getter/setter and
// also via the int adapters QML uses (osdStyleInt / overlayDisplayModeInt).

OsdStyle Settings::osdStyle() const
{
    return static_cast<OsdStyle>(
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey()));
}
int Settings::osdStyleInt() const
{
    return static_cast<int>(osdStyle());
}
void Settings::setOsdStyle(OsdStyle style)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey());
    m_store->write(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey(), static_cast<int>(style));
    const int after = m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey());
    if (after == before) {
        return;
    }
    Q_EMIT osdStyleChanged();
    Q_EMIT settingsChanged();
}
void Settings::setOsdStyleInt(int style)
{
    if (style >= 0 && style <= static_cast<int>(OsdStyle::Preview)) {
        setOsdStyle(static_cast<OsdStyle>(style));
    }
}

OverlayDisplayMode Settings::overlayDisplayMode() const
{
    return static_cast<OverlayDisplayMode>(
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey()));
}
int Settings::overlayDisplayModeInt() const
{
    return static_cast<int>(overlayDisplayMode());
}
void Settings::setOverlayDisplayMode(OverlayDisplayMode mode)
{
    const int before =
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey());
    m_store->write(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey(),
                   static_cast<int>(mode));
    const int after =
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT overlayDisplayModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setOverlayDisplayModeInt(int mode)
{
    if (mode >= 0 && mode <= static_cast<int>(OverlayDisplayMode::LayoutPreview)) {
        setOverlayDisplayMode(static_cast<OverlayDisplayMode>(mode));
    }
}

// filterLayoutsByAspectRatio sits in snappingBehaviorDisplayGroup with the
// other display settings — NOTIFY signal is filterLayoutsByAspectRatioChanged.
P_STORE_GET(bool, filterLayoutsByAspectRatio, snappingBehaviorDisplayGroup, filterByAspectRatioKey, bool)
P_STORE_SET_BOOL(setFilterLayoutsByAspectRatio, snappingBehaviorDisplayGroup, filterByAspectRatioKey,
                 filterLayoutsByAspectRatioChanged)

// ── Global exclusion knobs (PhosphorConfig::Store-backed) ──────────────────
//
// Three global behavioural knobs survive in the `Exclusions` group:
// `excludeTransientWindows` (boolean), `minimumWindowWidth` (int px),
// `minimumWindowHeight` (int px). Per-app / per-class exclusion lists
// retired in v4 — the migration in src/config/configmigration.cpp drains
// the legacy lists into Application-subject Rules, and runtime
// evaluators in SnapEngine, the KWin effect, and the WTA
// pending-restore prune route through `PhosphorRules::ExclusionRules`
// over the unified store.
//
// The on-disk group name `"Exclusions"` is INTENTIONALLY kept after the
// UI moved these knobs into a card relabeled "Window filtering" on the
// General page (see src/settings/qml/GeneralPage.qml). Renaming the
// group would require a v4→v5 schema migration to remap every existing
// user config without losing the three preserved knobs — disproportionate
// for a label change. The accessor name `exclusionsGroup()` keeps the
// disk shape, and the runtime UI label is independent.

P_STORE_GET(bool, excludeTransientWindows, exclusionsGroup, transientWindowsKey, bool)
P_STORE_SET_BOOL(setExcludeTransientWindows, exclusionsGroup, transientWindowsKey, excludeTransientWindowsChanged)
P_STORE_GET(int, minimumWindowWidth, exclusionsGroup, minimumWindowWidthKey, int)
P_STORE_SET_INT(setMinimumWindowWidth, exclusionsGroup, minimumWindowWidthKey, minimumWindowWidthChanged)
P_STORE_GET(int, minimumWindowHeight, exclusionsGroup, minimumWindowHeightKey, int)
P_STORE_SET_INT(setMinimumWindowHeight, exclusionsGroup, minimumWindowHeightKey, minimumWindowHeightChanged)

// ── Animation Window Filtering (PhosphorConfig::Store-backed) ──────────────
//
// Four global animation-filtering knobs in `Animations.WindowFiltering`:
// `animationExcludeTransientWindows`, `animationExcludeNotificationsAndOsd`,
// `animationMinimumWindowWidth`, `animationMinimumWindowHeight`. Mirrors
// the Exclusions block above (plus a NotificationsAndOsd knob), stored
// independently so a user can disable animations for an app while still
// snapping it (or vice versa). Per-app / per-class animation exclusion
// lists retired in v4 — they fold into ExcludeAnimations Rules.

P_STORE_GET(bool, animationExcludeTransientWindows, animationsWindowFilteringGroup, transientWindowsKey, bool)
P_STORE_SET_BOOL(setAnimationExcludeTransientWindows, animationsWindowFilteringGroup, transientWindowsKey,
                 animationExcludeTransientWindowsChanged)
P_STORE_GET(bool, animationExcludeNotificationsAndOsd, animationsWindowFilteringGroup, notificationsAndOsdKey, bool)
P_STORE_SET_BOOL(setAnimationExcludeNotificationsAndOsd, animationsWindowFilteringGroup, notificationsAndOsdKey,
                 animationExcludeNotificationsAndOsdChanged)
P_STORE_GET(int, animationMinimumWindowWidth, animationsWindowFilteringGroup, minimumWindowWidthKey, int)
P_STORE_SET_INT(setAnimationMinimumWindowWidth, animationsWindowFilteringGroup, minimumWindowWidthKey,
                animationMinimumWindowWidthChanged)
P_STORE_GET(int, animationMinimumWindowHeight, animationsWindowFilteringGroup, minimumWindowHeightKey, int)
P_STORE_SET_INT(setAnimationMinimumWindowHeight, animationsWindowFilteringGroup, minimumWindowHeightKey,
                animationMinimumWindowHeightChanged)

// ── Decoration Window Filtering (PhosphorConfig::Store-backed) ─────────────
//
// Three global decoration-filtering knobs in `Decorations.WindowFiltering`:
// `decorationExcludeTransientWindows`, `decorationMinimumWindowWidth`,
// `decorationMinimumWindowHeight`. Mirrors the Exclusions block, stored
// independently so the KWin effect's border pass can be tuned separately
// from snapping and animation filtering. Reuses the shared leaf keys; only
// the group differs. Consumed effect-side via the generic settingsChanged
// D-Bus broadcast (see kwin-effect loadCachedSettings).

P_STORE_GET(bool, decorationExcludeTransientWindows, decorationsWindowFilteringGroup, transientWindowsKey, bool)
P_STORE_SET_BOOL(setDecorationExcludeTransientWindows, decorationsWindowFilteringGroup, transientWindowsKey,
                 decorationExcludeTransientWindowsChanged)
P_STORE_GET(int, decorationMinimumWindowWidth, decorationsWindowFilteringGroup, minimumWindowWidthKey, int)
P_STORE_SET_INT(setDecorationMinimumWindowWidth, decorationsWindowFilteringGroup, minimumWindowWidthKey,
                decorationMinimumWindowWidthChanged)
P_STORE_GET(int, decorationMinimumWindowHeight, decorationsWindowFilteringGroup, minimumWindowHeightKey, int)
P_STORE_SET_INT(setDecorationMinimumWindowHeight, decorationsWindowFilteringGroup, minimumWindowHeightKey,
                decorationMinimumWindowHeightChanged)

// animationExcludedApplications / animationExcludedWindowClasses (+ their
// add*/remove* convenience methods) retired in v4 — the v4 migration drains
// the Animations.WindowFiltering group's Applications / WindowClasses leaves
// into `ExcludeAnimations` Rules. The settings accessors had no
// consumers after the KWin effect rewired off the loadSettingAsync fetches.

// ── PhosphorZones::Zone Selector (PhosphorConfig::Store-backed) ────────────────────────────
// Three enum-ints exposed via both the typed setter and an Int adapter for
// QML binding. Stored as int, the schema clamps the range.

P_STORE_GET(bool, zoneSelectorEnabled, snappingZoneSelectorGroup, enabledKey, bool)
P_STORE_SET_BOOL(setZoneSelectorEnabled, snappingZoneSelectorGroup, enabledKey, zoneSelectorEnabledChanged)
P_STORE_GET(int, zoneSelectorTriggerDistance, snappingZoneSelectorGroup, triggerDistanceKey, int)
P_STORE_SET_INT(setZoneSelectorTriggerDistance, snappingZoneSelectorGroup, triggerDistanceKey,
                zoneSelectorTriggerDistanceChanged)

ZoneSelectorPosition Settings::zoneSelectorPosition() const
{
    return static_cast<ZoneSelectorPosition>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey()));
}
int Settings::zoneSelectorPositionInt() const
{
    return static_cast<int>(zoneSelectorPosition());
}
void Settings::setZoneSelectorPosition(ZoneSelectorPosition value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey(), static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorPositionChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorPositionInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorPosition::BottomRight)) {
        setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(value));
    }
}

ZoneSelectorLayoutMode Settings::zoneSelectorLayoutMode() const
{
    return static_cast<ZoneSelectorLayoutMode>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey()));
}
int Settings::zoneSelectorLayoutModeInt() const
{
    return static_cast<int>(zoneSelectorLayoutMode());
}
void Settings::setZoneSelectorLayoutMode(ZoneSelectorLayoutMode value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey(),
                   static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorLayoutModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorLayoutModeInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) {
        setZoneSelectorLayoutMode(static_cast<ZoneSelectorLayoutMode>(value));
    }
}

P_STORE_GET(int, zoneSelectorPreviewWidth, snappingZoneSelectorGroup, previewWidthKey, int)
P_STORE_SET_INT(setZoneSelectorPreviewWidth, snappingZoneSelectorGroup, previewWidthKey,
                zoneSelectorPreviewWidthChanged)
P_STORE_GET(int, zoneSelectorPreviewHeight, snappingZoneSelectorGroup, previewHeightKey, int)
P_STORE_SET_INT(setZoneSelectorPreviewHeight, snappingZoneSelectorGroup, previewHeightKey,
                zoneSelectorPreviewHeightChanged)
P_STORE_GET(bool, zoneSelectorPreviewLockAspect, snappingZoneSelectorGroup, previewLockAspectKey, bool)
P_STORE_SET_BOOL(setZoneSelectorPreviewLockAspect, snappingZoneSelectorGroup, previewLockAspectKey,
                 zoneSelectorPreviewLockAspectChanged)
P_STORE_GET(int, zoneSelectorGridColumns, snappingZoneSelectorGroup, gridColumnsKey, int)
P_STORE_SET_INT(setZoneSelectorGridColumns, snappingZoneSelectorGroup, gridColumnsKey, zoneSelectorGridColumnsChanged)

ZoneSelectorSizeMode Settings::zoneSelectorSizeMode() const
{
    return static_cast<ZoneSelectorSizeMode>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey()));
}
int Settings::zoneSelectorSizeModeInt() const
{
    return static_cast<int>(zoneSelectorSizeMode());
}
void Settings::setZoneSelectorSizeMode(ZoneSelectorSizeMode value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey(), static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorSizeModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorSizeModeInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorSizeMode::Manual)) {
        setZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(value));
    }
}

P_STORE_GET(int, zoneSelectorMaxRows, snappingZoneSelectorGroup, maxRowsKey, int)
P_STORE_SET_INT(setZoneSelectorMaxRows, snappingZoneSelectorGroup, maxRowsKey, zoneSelectorMaxRowsChanged)

// ISnapSettings hook for the snap engine — same Snapping.Behavior value, surfaced
// under the interface name the engine consults on AutoRestored commits.
bool Settings::focusNewWindows() const
{
    return snappingFocusNewWindows();
}
bool Settings::unfloatFallbackToZone() const
{
    return snapUnfloatFallbackToZone();
}
P_STORE_GET(bool, autotileRespectMinimumSize, tilingBehaviorGroup, respectMinimumSizeKey, bool)
P_STORE_SET_BOOL(setAutotileRespectMinimumSize, tilingBehaviorGroup, respectMinimumSizeKey,
                 autotileRespectMinimumSizeChanged)

Settings::AutotileInsertPosition Settings::autotileInsertPosition() const
{
    return static_cast<AutotileInsertPosition>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey()));
}
int Settings::autotileInsertPositionInt() const
{
    return static_cast<int>(autotileInsertPosition());
}
void Settings::setAutotileInsertPosition(AutotileInsertPosition position)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey(),
                   static_cast<int>(position));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileInsertPositionChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileInsertPositionInt(int position)
{
    if (position >= ConfigDefaults::autotileInsertPositionMin()
        && position <= ConfigDefaults::autotileInsertPositionMax()) {
        setAutotileInsertPosition(static_cast<AutotileInsertPosition>(position));
    }
}

StickyWindowHandling Settings::autotileStickyWindowHandling() const
{
    return static_cast<StickyWindowHandling>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey()));
}
int Settings::autotileStickyWindowHandlingInt() const
{
    return static_cast<int>(autotileStickyWindowHandling());
}
void Settings::setAutotileStickyWindowHandling(StickyWindowHandling handling)
{
    const int before =
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey(),
                   static_cast<int>(handling));
    const int after =
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileStickyWindowHandlingChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileStickyWindowHandlingInt(int handling)
{
    if (handling >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
        && handling <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
        setAutotileStickyWindowHandling(static_cast<StickyWindowHandling>(handling));
    }
}

AutotileDragBehavior Settings::autotileDragBehavior() const
{
    return static_cast<AutotileDragBehavior>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey()));
}
int Settings::autotileDragBehaviorInt() const
{
    return static_cast<int>(autotileDragBehavior());
}
void Settings::setAutotileDragBehavior(AutotileDragBehavior behavior)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey(),
                   static_cast<int>(behavior));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileDragBehaviorChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileDragBehaviorInt(int behavior)
{
    if (behavior == static_cast<int>(AutotileDragBehavior::Float)
        || behavior == static_cast<int>(AutotileDragBehavior::Reorder)) {
        setAutotileDragBehavior(static_cast<AutotileDragBehavior>(behavior));
    }
}

AutotileOverflowBehavior Settings::autotileOverflowBehavior() const
{
    return static_cast<AutotileOverflowBehavior>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey()));
}
int Settings::autotileOverflowBehaviorInt() const
{
    return static_cast<int>(autotileOverflowBehavior());
}
void Settings::setAutotileOverflowBehavior(AutotileOverflowBehavior behavior)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey(),
                   static_cast<int>(behavior));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileOverflowBehaviorChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileOverflowBehaviorInt(int behavior)
{
    if (behavior == static_cast<int>(AutotileOverflowBehavior::Float)
        || behavior == static_cast<int>(AutotileOverflowBehavior::Unlimited)) {
        setAutotileOverflowBehavior(static_cast<AutotileOverflowBehavior>(behavior));
    }
}

} // namespace PlasmaZones
