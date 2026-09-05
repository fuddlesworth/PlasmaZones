// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"

#include <array>

namespace PlasmaZones {

// ── Workspaces (PhosphorConfig::Store-backed) ───────────────────────────────
// Dynamic per-monitor workspaces: the gate scalars, the named-workspace
// declarations, the ten verb chords, and the three indexed slot families
// (focus chord, move chord, target). Defaults live in
// configdefaults_workspaces.h; the schema groups in
// settingsschema_workspaces.cpp and (for the Shortcuts.Global chord leaves)
// settingsschema.cpp's appendShortcutsSchema.

P_STORE_GET(bool, workspacesEnabled, workspacesBehaviorGroup, enabledKey, bool)
P_STORE_SET_BOOL(setWorkspacesEnabled, workspacesBehaviorGroup, enabledKey, workspacesEnabledChanged)

P_STORE_GET(bool, workspacesManageKWinPerOutput, workspacesBehaviorGroup, manageKWinPerOutputKey, bool)
P_STORE_SET_BOOL(setWorkspacesManageKWinPerOutput, workspacesBehaviorGroup, manageKWinPerOutputKey,
                 workspacesManageKWinPerOutputChanged)

P_STORE_GET(bool, workspacesSnapBackOsdHint, workspacesBehaviorGroup, snapBackOsdHintKey, bool)
P_STORE_SET_BOOL(setWorkspacesSnapBackOsdHint, workspacesBehaviorGroup, snapBackOsdHintKey,
                 workspacesSnapBackOsdHintChanged)

P_STORE_GET(bool, workspacesRebindKWinShortcuts, workspacesBehaviorGroup, rebindKWinShortcutsKey, bool)
P_STORE_SET_BOOL(setWorkspacesRebindKWinShortcuts, workspacesBehaviorGroup, rebindKWinShortcutsKey,
                 workspacesRebindKWinShortcutsChanged)

// Workspaces.Overview: the overview effect's look and input. Read by the
// KWin effect over the settings wire (see the adaptor registry). ISettings
// carries defaulted getters for these five (its header does not see
// ConfigDefaults), so the agreement is pinned here the way scrolling.cpp
// pins the tab indicator's; the backdrop string is checked in the
// settings-defaults test since a QString cannot be a constant expression.
static_assert(ConfigDefaults::overviewZoom() == 0.5,
              "ISettings::overviewZoom defaults to 0.5 — update it with this default");
static_assert(ConfigDefaults::overviewGestureEnabled(),
              "ISettings::overviewGestureEnabled defaults to true — update it with this default");
static_assert(ConfigDefaults::overviewWheelSwitchesWorkspaces(),
              "ISettings::overviewWheelSwitchesWorkspaces defaults to true — update it with this default");
static_assert(ConfigDefaults::overviewShowWorkspaceNames(),
              "ISettings::overviewShowWorkspaceNames defaults to true — update it with this default");
P_STORE_GET(qreal, overviewZoom, workspacesOverviewGroup, overviewZoomKey, double)
P_STORE_SET_DOUBLE(setOverviewZoom, workspacesOverviewGroup, overviewZoomKey, overviewZoomChanged)
P_STORE_GET(QString, overviewBackdropColor, workspacesOverviewGroup, overviewBackdropColorKey, QString)
P_STORE_SET_STRING(setOverviewBackdropColor, workspacesOverviewGroup, overviewBackdropColorKey,
                   overviewBackdropColorChanged)
P_STORE_GET(bool, overviewGestureEnabled, workspacesOverviewGroup, overviewGestureEnabledKey, bool)
P_STORE_SET_BOOL(setOverviewGestureEnabled, workspacesOverviewGroup, overviewGestureEnabledKey,
                 overviewGestureEnabledChanged)
P_STORE_GET(bool, overviewWheelSwitchesWorkspaces, workspacesOverviewGroup, overviewWheelSwitchesWorkspacesKey, bool)
P_STORE_SET_BOOL(setOverviewWheelSwitchesWorkspaces, workspacesOverviewGroup, overviewWheelSwitchesWorkspacesKey,
                 overviewWheelSwitchesWorkspacesChanged)
P_STORE_GET(bool, overviewShowWorkspaceNames, workspacesOverviewGroup, overviewShowWorkspaceNamesKey, bool)
P_STORE_SET_BOOL(setOverviewShowWorkspaceNames, workspacesOverviewGroup, overviewShowWorkspaceNamesKey,
                 overviewShowWorkspaceNamesChanged)

// Named-workspace declarations: whole-replace QVariantList composite, same
// write shape as the trigger lists (read-back compare so a semantically
// identical list swallows no signal).
QVariantList Settings::workspacesNamedEntries() const
{
    return m_store->readVariant(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey()).toList();
}

void Settings::setWorkspacesNamedEntries(const QVariantList& entries)
{
    // Same stale-read guard every sibling composite setter takes: the
    // read-back compare below decides whether anything changed, so it must
    // not compare against a backend copy another process has already moved
    // past. See refreshCleanBackendFromDisk's definition for the full
    // cross-process coherence rationale.
    refreshCleanBackendFromDisk();
    const QVariantList before =
        m_store->readVariant(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey()).toList();
    m_store->write(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey(), entries);
    const QVariantList after =
        m_store->readVariant(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey()).toList();
    if (before == after) {
        return;
    }
    Q_EMIT workspacesNamedEntriesChanged();
    Q_EMIT settingsChanged();
}

// ── Verb shortcuts (Shortcuts.Global leaves, like every other chord) ────────

P_STORE_GET(QString, workspaceFocusUpShortcut, shortcutsGlobalGroup, workspaceFocusUpKey, QString)
P_STORE_GET(QString, overviewToggleShortcut, shortcutsGlobalGroup, overviewToggleKey, QString)
P_STORE_SET_STRING(setOverviewToggleShortcut, shortcutsGlobalGroup, overviewToggleKey, overviewToggleShortcutChanged)
P_STORE_SET_STRING(setWorkspaceFocusUpShortcut, shortcutsGlobalGroup, workspaceFocusUpKey,
                   workspaceFocusUpShortcutChanged)
P_STORE_GET(QString, workspaceFocusDownShortcut, shortcutsGlobalGroup, workspaceFocusDownKey, QString)
P_STORE_SET_STRING(setWorkspaceFocusDownShortcut, shortcutsGlobalGroup, workspaceFocusDownKey,
                   workspaceFocusDownShortcutChanged)
P_STORE_GET(QString, workspaceMoveWindowUpShortcut, shortcutsGlobalGroup, workspaceMoveWindowUpKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveWindowUpShortcut, shortcutsGlobalGroup, workspaceMoveWindowUpKey,
                   workspaceMoveWindowUpShortcutChanged)
P_STORE_GET(QString, workspaceMoveWindowDownShortcut, shortcutsGlobalGroup, workspaceMoveWindowDownKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveWindowDownShortcut, shortcutsGlobalGroup, workspaceMoveWindowDownKey,
                   workspaceMoveWindowDownShortcutChanged)
P_STORE_GET(QString, workspaceMoveColumnUpShortcut, shortcutsGlobalGroup, workspaceMoveColumnUpKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveColumnUpShortcut, shortcutsGlobalGroup, workspaceMoveColumnUpKey,
                   workspaceMoveColumnUpShortcutChanged)
P_STORE_GET(QString, workspaceMoveColumnDownShortcut, shortcutsGlobalGroup, workspaceMoveColumnDownKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveColumnDownShortcut, shortcutsGlobalGroup, workspaceMoveColumnDownKey,
                   workspaceMoveColumnDownShortcutChanged)
P_STORE_GET(QString, workspaceReorderUpShortcut, shortcutsGlobalGroup, workspaceReorderUpKey, QString)
P_STORE_SET_STRING(setWorkspaceReorderUpShortcut, shortcutsGlobalGroup, workspaceReorderUpKey,
                   workspaceReorderUpShortcutChanged)
P_STORE_GET(QString, workspaceReorderDownShortcut, shortcutsGlobalGroup, workspaceReorderDownKey, QString)
P_STORE_SET_STRING(setWorkspaceReorderDownShortcut, shortcutsGlobalGroup, workspaceReorderDownKey,
                   workspaceReorderDownShortcutChanged)
P_STORE_GET(QString, workspaceMoveToMonitorLeftShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorLeftKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveToMonitorLeftShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorLeftKey,
                   workspaceMoveToMonitorLeftShortcutChanged)
P_STORE_GET(QString, workspaceMoveToMonitorRightShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorRightKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveToMonitorRightShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorRightKey,
                   workspaceMoveToMonitorRightShortcutChanged)

// ── Indexed workspace slots ─────────────────────────────────────────────────
// Nine focus chords + nine move chords + nine targets, addressed 0-based. The
// three pairs of accessors are one shape, so they share the read/write
// primitives below — the writeTriggerList precedent: the bounds contract and
// the changed-check live in one place instead of three copies.
//
// One shared NOTIFY per chord family: the shortcuts page re-reads all fields
// on it, and the ShortcutManager's rebind rides settingsChanged regardless.

namespace {
/// One of the three ConfigDefaults key builders (1-based slot → key string).
using SlotKeyFn = QString (*)(int);

QString readSlotValue(PhosphorConfig::Store* store, const QString& group, SlotKeyFn keyFn, int index)
{
    // Out-of-range reads answer empty rather than qFatal-ing through the key
    // builder: the index arrives from QML, where a stale model row can outlive
    // the page that produced it.
    if (index < 0 || index >= ConfigDefaults::WorkspaceSlotCount) {
        return {};
    }
    return store->read<QString>(group, keyFn(index + 1));
}

/// Returns true when the store actually moved — the caller emits on that.
bool writeSlotValue(PhosphorConfig::Store* store, const QString& group, SlotKeyFn keyFn, int index,
                    const QString& value)
{
    if (index < 0 || index >= ConfigDefaults::WorkspaceSlotCount) {
        return false;
    }
    const QString key = keyFn(index + 1);
    if (store->read<QString>(group, key) == value) {
        return false;
    }
    store->write(group, key, value);
    return true;
}
} // namespace

QString Settings::workspaceSlotTarget(int index) const
{
    return readSlotValue(m_store.get(), ConfigDefaults::workspacesSlotsGroup(), &ConfigDefaults::workspaceSlotTargetKey,
                         index);
}

void Settings::setWorkspaceSlotTarget(int index, const QString& workspaceName)
{
    // Trimmed to match how a declaration's `name` is canonicalized
    // (canonicalNamedEntries in settingsschema_workspaces.cpp trims before it
    // stores), so a target typed with a stray space still resolves.
    //
    // No existence check against the declaration list: a target naming a
    // workspace that does not exist (yet) is inert, not broken. The daemon
    // resolves the name at press time — WorkspaceController::
    // moveWindowToNamedWorkspace looks the name up via desktopIdForName and
    // returns without acting when it comes back empty — so a slot pointing at
    // a since-renamed workspace simply does nothing until the name exists
    // again. Validating here would instead delete the user's assignment
    // whenever the two are edited out of order.
    if (!writeSlotValue(m_store.get(), ConfigDefaults::workspacesSlotsGroup(), &ConfigDefaults::workspaceSlotTargetKey,
                        index, workspaceName.trimmed())) {
        return;
    }
    Q_EMIT workspaceSlotTargetsChanged();
    Q_EMIT settingsChanged();
}

QString Settings::workspaceFocusSlotShortcut(int index) const
{
    return readSlotValue(m_store.get(), ConfigDefaults::shortcutsGlobalGroup(), &ConfigDefaults::workspaceFocusSlotKey,
                         index);
}

void Settings::setWorkspaceFocusSlotShortcut(int index, const QString& shortcut)
{
    if (!writeSlotValue(m_store.get(), ConfigDefaults::shortcutsGlobalGroup(), &ConfigDefaults::workspaceFocusSlotKey,
                        index, shortcut)) {
        return;
    }
    Q_EMIT workspaceSlotShortcutsChanged();
    Q_EMIT settingsChanged();
}

QString Settings::workspaceMoveSlotShortcut(int index) const
{
    return readSlotValue(m_store.get(), ConfigDefaults::shortcutsGlobalGroup(), &ConfigDefaults::workspaceMoveSlotKey,
                         index);
}

void Settings::setWorkspaceMoveSlotShortcut(int index, const QString& shortcut)
{
    if (!writeSlotValue(m_store.get(), ConfigDefaults::shortcutsGlobalGroup(), &ConfigDefaults::workspaceMoveSlotKey,
                        index, shortcut)) {
        return;
    }
    Q_EMIT workspaceSlotShortcutsChanged();
    Q_EMIT settingsChanged();
}

// ── Change detection for the non-Q_PROPERTY workspace keys ──────────────────
// The ten verb chords and the three indexed slot families are reached through
// plain getters and Q_INVOKABLEs, not Q_PROPERTYs, so
// snapshotNotifyProperties() cannot see them: a load(), a staged profile
// overlay, or a per-page Reset/Discard that moved only these keys fired no
// signal at all, leaving the shortcuts page painting stale chords and — worse
// — leaving ShortcutManager bound to the previous chords, because its rebind
// rides settingsChanged.
//
// These two functions are the store-delta twin of the Q_PROPERTY pair, with
// the same contract: snapshot BEFORE the mutation, emit after, and report
// whether anything fired so the caller folds it into its one settingsChanged.
//
// The verb half emits per-verb NOTIFYs (each has its own signal, and the
// settings page binds them individually); the slot half collapses onto the two
// shared family signals, which is what their setters emit too.

namespace {
/// The eleven verb chords, paired getter → NOTIFY, in one table so the snapshot
/// and the emit walk the same order. Adding a verb means one row here, not two
/// hand-kept lists.
struct WorkspaceVerbChord
{
    QString (Settings::*get)() const;
    void (Settings::*changed)();
};

const std::array<WorkspaceVerbChord, 11>& workspaceVerbChords()
{
    static const std::array<WorkspaceVerbChord, 11> table{{
        {&Settings::workspaceFocusUpShortcut, &Settings::workspaceFocusUpShortcutChanged},
        {&Settings::workspaceFocusDownShortcut, &Settings::workspaceFocusDownShortcutChanged},
        {&Settings::workspaceMoveWindowUpShortcut, &Settings::workspaceMoveWindowUpShortcutChanged},
        {&Settings::workspaceMoveWindowDownShortcut, &Settings::workspaceMoveWindowDownShortcutChanged},
        {&Settings::workspaceMoveColumnUpShortcut, &Settings::workspaceMoveColumnUpShortcutChanged},
        {&Settings::workspaceMoveColumnDownShortcut, &Settings::workspaceMoveColumnDownShortcutChanged},
        {&Settings::workspaceReorderUpShortcut, &Settings::workspaceReorderUpShortcutChanged},
        {&Settings::workspaceReorderDownShortcut, &Settings::workspaceReorderDownShortcutChanged},
        {&Settings::workspaceMoveToMonitorLeftShortcut, &Settings::workspaceMoveToMonitorLeftShortcutChanged},
        {&Settings::workspaceMoveToMonitorRightShortcut, &Settings::workspaceMoveToMonitorRightShortcutChanged},
        {&Settings::overviewToggleShortcut, &Settings::overviewToggleShortcutChanged},
    }};
    return table;
}
} // namespace

QVector<QString> Settings::snapshotWorkspaceKeyFamilies() const
{
    const auto& verbs = workspaceVerbChords();
    QVector<QString> snapshot;
    snapshot.reserve(static_cast<int>(verbs.size()) + 3 * ConfigDefaults::WorkspaceSlotCount);
    for (const WorkspaceVerbChord& verb : verbs) {
        snapshot.append((this->*(verb.get))());
    }
    // Fixed order — verbs, focus chords, move chords, targets — so
    // emitChangedWorkspaceKeyFamilies can index straight back into it.
    for (int i = 0; i < ConfigDefaults::WorkspaceSlotCount; ++i) {
        snapshot.append(workspaceFocusSlotShortcut(i));
    }
    for (int i = 0; i < ConfigDefaults::WorkspaceSlotCount; ++i) {
        snapshot.append(workspaceMoveSlotShortcut(i));
    }
    for (int i = 0; i < ConfigDefaults::WorkspaceSlotCount; ++i) {
        snapshot.append(workspaceSlotTarget(i));
    }
    return snapshot;
}

bool Settings::emitChangedWorkspaceKeyFamilies(const QVector<QString>& before)
{
    const auto& verbs = workspaceVerbChords();
    const int verbCount = static_cast<int>(verbs.size());
    const int slots = ConfigDefaults::WorkspaceSlotCount;
    // A short snapshot means the caller paired the two calls across a change
    // to the families themselves; emitting off a mismatched index would fire
    // the wrong verb's NOTIFY, so bail rather than guess.
    if (before.size() != verbCount + 3 * slots) {
        return false;
    }
    bool anyChanged = false;
    for (int i = 0; i < verbCount; ++i) {
        if ((this->*(verbs[i].get))() != before.at(i)) {
            anyChanged = true;
            (this->*(verbs[i].changed))();
        }
    }
    bool chordsChanged = false;
    for (int i = 0; i < slots; ++i) {
        if (workspaceFocusSlotShortcut(i) != before.at(verbCount + i)
            || workspaceMoveSlotShortcut(i) != before.at(verbCount + slots + i)) {
            chordsChanged = true;
            break;
        }
    }
    if (chordsChanged) {
        anyChanged = true;
        Q_EMIT workspaceSlotShortcutsChanged();
    }
    bool targetsChanged = false;
    for (int i = 0; i < slots; ++i) {
        if (workspaceSlotTarget(i) != before.at(verbCount + 2 * slots + i)) {
            targetsChanged = true;
            break;
        }
    }
    if (targetsChanged) {
        anyChanged = true;
        Q_EMIT workspaceSlotTargetsChanged();
    }
    return anyChanged;
}

} // namespace PlasmaZones
