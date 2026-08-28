// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Page-class predicates and shared-domain config key lists used across the
// settings-app translation units. Each declaration in the header names its own
// consumers; they are not all the same pair.
//
// These lived in an anonymous namespace inside _pagestate.cpp until that file
// was split; internal linkage cannot span two translation units, so they are
// promoted here rather than duplicated. Same reason animationpagescope /
// decorationpagescope are their own pair: keeping one definition of "which
// pages are animation pages" is what stops the dirty check and the reset from
// ever disagreeing about a page's class.

#include "settingscontroller_pagekeys.h"

#include "settingscontroller.h"

#include "config/configdefaults.h"

#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace PlasmaZones {

// The drag-to-reorder pages. Their state is the staged order optional, not
// config-manifest keys, so per-page Reset/Discard dispatches to the ordering
// helpers rather than resetKeys/discardKeys.
OrderingPageKind orderingPageKind(const QString& page)
{
    if (page == QLatin1String("snapping-ordering")) {
        return OrderingPageKind::Snapping;
    }
    if (page == QLatin1String("tiling-ordering")) {
        return OrderingPageKind::Tiling;
    }
    if (page == QLatin1String("scrolling-ordering")) {
        return OrderingPageKind::Scrolling;
    }
    return OrderingPageKind::None;
}

// The three Quick Shortcuts pages. Their editable state is the per-mode staged
// quick-slot layout assignments in StagingService (daemon-backed).
bool isShortcutsPage(const QString& page)
{
    return page == QLatin1String("snapping-shortcuts") || page == QLatin1String("tiling-shortcuts")
        || page == QLatin1String("scrolling-shortcuts");
}

// The per-mode library pages. Their stores (layouts / algorithms / templates)
// write immediately rather than staging, so the pages are never dirty — see the
// header's note for how their set-as-default config write is attributed.
bool isLibraryPage(const QString& page)
{
    return page == QLatin1String("snapping-layouts") || page == QLatin1String("tiling-library")
        || page == QLatin1String("scrolling-templates");
}

// QML bridge for the predicate above; see the header note on the Q_INVOKABLE.
bool SettingsController::isLibraryPage(const QString& page) const
{
    return PlasmaZones::isLibraryPage(page);
}

bool SettingsController::workspacesAtCap() const
{
    return m_workspaceVdm
        && m_workspaceVdm->desktopCount() >= PhosphorWorkspaces::WorkspaceReconciler::DefaultDesktopCap;
}

// The DEFAULT cap, and CONSTANT for that reason. The running reconciler keeps
// its own mutable copy (m_desktopCap, which setDesktopCap rewrites once the
// controller has probed what the compositor actually accepts) and the settings
// app has no link to that object, so a probed cap never reaches here. What QML
// uses this for is the authoring bound on a declaration's Position, which the
// daemon clamps again against the live cap on its own side.
int SettingsController::workspacesDesktopCap() const
{
    return PhosphorWorkspaces::WorkspaceReconciler::DefaultDesktopCap;
}

int SettingsController::workspaceSlotCount() const
{
    return ConfigDefaults::WorkspaceSlotCount;
}

// Carry a rename of a named workspace through the quick-slot targets.
//
// The workspace NAME is the wire value downstream: a quick slot stores the name
// it sends the window to (Workspaces.Slots/TargetN), which the daemon resolves
// at press time. Settings deliberately never validates a target, precisely so
// a declaration and the slot naming it can be edited in either order, so a
// rename on the Named Workspaces page would otherwise leave every slot
// pointing at the old name silently inert. Following the rename is what the
// user means, and the slot keeps pointing at the workspace they renamed.
//
// Rules are NOT carried along. A RouteToWorkspace action stores the name too,
// but rules live in their own store with their own staging, and rewriting them
// from here would edit a document the user may have open elsewhere. Such a
// rule goes dormant until its action is re-pointed.
//
// It lives here rather than in the page's QML because of an OWNERSHIP seam.
// Those Target keys belong to the workspaces-shortcuts manifest, and
// pageOwnedConfigKeys holds a strict one-owner invariant, so co-listing them
// under workspaces-named is not open to us. Written straight from QML, the
// cascade badged the SHORTCUTS page dirty and left an edit that only that
// page's Discard could revert, while the page the user actually edited could
// not undo its own cascade. Recording the slots here lets
// discardPage("workspaces-named") revert exactly those keys to the committed
// baseline alongside its own.
//
// The record is never cleared on Save, and does not need to be: discardKeys
// reverts to the committed baseline, and after a Save that baseline already
// carries the rename, so a stale entry reverts nothing.
void SettingsController::renameWorkspaceSlotTargets(const QString& previousName, const QString& newName)
{
    const QString from = previousName.trimmed();
    const QString to = newName.trimmed();
    if (from.isEmpty() || to.isEmpty() || from == to) {
        return;
    }
    for (int index = 0; index < ConfigDefaults::WorkspaceSlotCount; ++index) {
        if (m_settings.workspaceSlotTarget(index) != from) {
            continue;
        }
        m_settings.setWorkspaceSlotTarget(index, to);
        // 1-based, the spelling ConfigDefaults::workspaceSlotTargetKey takes.
        m_renamedWorkspaceSlots.insert(index + 1);
    }
}

bool SettingsController::kwinPerOutputDesktopsEnabled() const
{
    // Flat-INI read of KWin's per-output-desktops flag; the daemon's gate
    // (WorkspaceController::kwinPerOutputEnabled) is the documented twin.
    //
    // Two KConfig spellings QSettings does not decode on its own, both of
    // which would otherwise read as "off" and put the consent warning back in
    // front of a user who already has the setting on:
    //   - an immutability / locale marker on the key ("PerOutputVirtualDesktops[$i]"),
    //     which QSettings keeps as part of the key name;
    //   - KConfig's boolean words, which include "true"/"on"/"yes"/"1";
    //     QVariant::toBool only understands the first and last of those.
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    const QSettings kwinrc(path, QSettings::IniFormat);
    const QString group = QStringLiteral("Windows/");
    const QString base = QStringLiteral("PerOutputVirtualDesktops");
    QVariant raw = kwinrc.value(group + base);
    if (!raw.isValid()) {
        // Scan for the same key carrying any KConfig marker suffix.
        const QStringList keys = kwinrc.allKeys();
        for (const QString& key : keys) {
            if (key.startsWith(group + base + QLatin1Char('['))) {
                raw = kwinrc.value(key);
                break;
            }
        }
    }
    if (!raw.isValid()) {
        return false;
    }
    const QString text = raw.toString().trimmed();
    return text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0 || text == QLatin1String("1");
}

// Every animation leaf shares the single AnimationsPageController staging domain
// AND the single ShaderProfileTree key, but Reset/Discard/dirty are NOT
// whole-tree: each surface leaf owns one event-path subtree (see
// animationPageScope, which is the list — do not maintain a second copy of it
// here), plus the condensed animations-simple page, which takes the same branch
// across several roots. The general leaf owns only the config keys, and the
// presets/sets/shaders library leaves act on the whole editable tree. Scoping keeps a Reset on one surface from
// wiping the others (mirrors the decoration domain below).
bool isAnimationPage(const QString& page)
{
    return SettingsController::pageGroupChildren().value(QStringLiteral("animations")).contains(page);
}

// The animation config keys OTHER than the per-event surfaces: the global
// enable, the baseline motion Profile blob, and window filtering. Owned by the
// Animations → General leaf. The ShaderProfileTree key is deliberately ABSENT —
// it is per-event state scoped through animationPageScope, not a General-page
// key. (The per-event override FILES are likewise not config keys.)
const Settings::ConfigKeyList& animationGeneralConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys{
        {CD::animationsGroup(), CD::enabledKey()},
        {CD::animationsGroup(), CD::animationProfileKey()},
        {CD::animationsWindowFilteringGroup(), CD::transientWindowsKey()},
        {CD::animationsWindowFilteringGroup(), CD::notificationsAndOsdKey()},
        {CD::animationsWindowFilteringGroup(), CD::minimumWindowWidthKey()},
        {CD::animationsWindowFilteringGroup(), CD::minimumWindowHeightKey()},
    };
    return keys;
}

// The WHOLE animation "value" surface — General's keys PLUS the ShaderProfileTree
// key. Used only by the non-surface library leaves (presets / motion sets / shaders), whose
// Reset/Discard act on the entire editable tree (paired with clearAllOverrides /
// revertPending for the per-event FILES).
const Settings::ConfigKeyList& animationConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys = []() {
        Settings::ConfigKeyList k = animationGeneralConfigKeys();
        k.append({CD::animationsGroup(), CD::shaderProfileTreeKey()});
        return k;
    }();
    return keys;
}

// AnimationPageScope / animationPageScope / animationPathUnderAny /
// animationPathInScope / animationScopedBuiltInPaths / shaderTreeScopeDiffers
// moved to animationpagescope.{h,cpp}: pure page→event-root scoping logic
// with no SettingsController dependency, split out for the same reason the
// decoration equivalents were (direct unit-testability without building the
// whole settings-app object graph, and to keep this translation unit inside
// the file-size ceiling).

// Every decoration leaf reads/writes the single shared DecorationProfileTree
// settings key (one JSON blob covering windows, OSDs, popups and shell
// surfaces), so pageGroupChildren("decorations") — the canonical leaf set —
// identifies them all. Reset/Discard/dirty are NOT whole-tree, though: the
// four surface pages
// each own one root subtree (see decorationSurfaceRoot), so resetting OSDs must
// not touch the Windows overrides. Only the sets/shaders library leaves act on
// the whole editable tree.
bool isDecorationPage(const QString& page)
{
    return SettingsController::pageGroupChildren().value(QStringLiteral("decorations")).contains(page);
}

// decorationSurfaceRoot / decorationPathInRoot / decorationRootDiffers moved
// to decorationpagescope.{h,cpp}: they are pure page→root scoping logic with
// no SettingsController dependency, split out so the root dispatch and
// root-scoped diffing are directly unit-testable
// (tests/unit/settings/test_decoration_page_scope.cpp) without constructing
// the whole settings-app object graph.

// The decoration "value" surface: one Store-backed key. It cannot ride the
// pageOwnedConfigKeys manifest — every decoration leaf would own the
// same key, violating the manifest's one-owner invariant — so the decoration
// branches in isPageDirty/resetPage/discardPage dispatch through this list
// instead. Unlike the animation domain there are no side files: reset/discard
// is entirely resetKeys/discardKeys, and the decorationProfileTreeChanged
// re-emit drives DecorationPageController::profilesChanged so open cards
// refresh.
//
// The Decorations.WindowFiltering knobs are NOT here — they live on the
// Decorations → General (window-appearance) page and ride that page's
// pageOwnedConfigKeys manifest entry, not this shared surface-tree domain.
const Settings::ConfigKeyList& decorationConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys{
        {CD::decorationsGroup(), CD::decorationProfileTreeKey()},
    };
    return keys;
}
} // namespace PlasmaZones
