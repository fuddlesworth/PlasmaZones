// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PlasmaZones {

class AnimationPresetLibrary;
class ISettings;
class MotionSetStore;

/// Q_PROPERTY surface for the "Animations" settings page.
///
/// Edits per-event motion-profile overrides and surfaces the built-in
/// `PhosphorAnimation::ProfilePaths` taxonomy as a section-grouped list
/// for the QML drilldown.
///
/// ## Persistence model
///
/// Per-event overrides live as one Profile JSON file per path under
/// `~/.local/share/plasmazones/profiles/`. The daemon's existing
/// `PhosphorAnimation::ProfileLoader` watches that dir and pushes files
/// into `PhosphorProfileRegistry` automatically — no daemon code changes.
/// User-wins-over-shipped semantics are already wired by the loader's
/// owner-tag partitioning. The settings app has its own bootstrap-owned
/// loader (see `src/settings/main.cpp`); both watchers respond to the
/// same dir, so a write here updates QML thumbnails in-process AND
/// live-updates the daemon at runtime.
///
/// ## Effective-value resolution
///
/// `resolvedProfile()` walks the path's parent chain through the
/// process-wide `PhosphorProfileRegistry::defaultRegistry()` (covers
/// shipped + user overrides) with library-default fill-in. When the
/// registry isn't published (unit tests without bootstrap), it falls
/// back to walking the user dir directly.
///
/// ## Composition
///
/// The controller delegates persistence-heavy concerns to two child
/// QObjects: `AnimationPresetLibrary` (preset CRUD) and `MotionSetStore`
/// (motion-set CRUD). Their signals are forwarded to the controller's
/// own signals via `connect()` so QML rebinds without poking at the
/// sub-services directly.
class AnimationsPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(qreal springOmegaMin READ springOmegaMin CONSTANT)
    Q_PROPERTY(qreal springOmegaMax READ springOmegaMax CONSTANT)
    Q_PROPERTY(qreal springZetaMin READ springZetaMin CONSTANT)
    Q_PROPERTY(qreal springZetaMax READ springZetaMax CONSTANT)

public:
    /// @param shaderRegistry Optional — when null, all `*ShaderEffects()` /
    ///        `*ShaderProfile()` Q_INVOKABLEs return empty results so unit
    ///        tests can construct the controller without an animation
    ///        bootstrap.
    /// @param settings Optional — when null, shader-tree CRUD is a no-op.
    explicit AnimationsPageController(PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry = nullptr,
                                      ISettings* settings = nullptr, QObject* parent = nullptr);
    ~AnimationsPageController() override;

    /// PhosphorControl::StagingDomain contract. The animations page's
    /// own pendingChanges API is the per-page staging — wire it through.
    /// dirtyChanged() (inherited from StagingDomain) is emitted alongside
    /// pendingChangesChanged() so ApplicationController can react.
    bool isDirty() const override;
    void apply() override;
    void discard() override;

    qreal springOmegaMin() const
    {
        return 0.1;
    }
    qreal springOmegaMax() const
    {
        return 200.0;
    }
    qreal springZetaMin() const
    {
        return 0.0;
    }
    qreal springZetaMax() const
    {
        return 10.0;
    }

    /// Built-in event paths, grouped by section. Each entry:
    /// ```
    /// { "section": "window", "label": "Window",
    ///   "paths": [ { "path": "window", "label": "Window (inherited)",
    ///                "parent": "global", "isCategory": true },
    ///              { "path": "window.open", "label": "Open",
    ///                "parent": "window", "isCategory": false }, ... ] }
    /// ```
    /// All built-in paths from `ProfilePaths::allBuiltInPaths()` are included.
    Q_INVOKABLE QVariantList eventSections() const;

    /// First dotted segment of @p path, or `"global"` when @p path is the
    /// global root. Drives the sidebar grouping.
    Q_INVOKABLE QString sectionForPath(const QString& path) const;

    /// Title-cased label for @p path's last segment (e.g. `"editor.snapIn"`
    /// → `"Snap In"`). Falls back to the segment itself if humanisation
    /// fails. Translation hook lives in QML; this is the raw English
    /// form.
    Q_INVOKABLE QString eventLabel(const QString& path) const;

    /// Wraps `ProfilePaths::parentPath`.
    Q_INVOKABLE QString parentPath(const QString& path) const;

    /// Inheritance chain from @p path up to (but excluding) the empty
    /// root. Useful for "snap → zone → global" breadcrumbs.
    Q_INVOKABLE QStringList parentChain(const QString& path) const;

    /// True iff a user override file exists for @p path. Returns false
    /// for any @p path that is not a built-in event path (rejecting
    /// traversal attempts).
    Q_INVOKABLE bool hasOverride(const QString& path) const;

    /// Per-path override file content as a QVariantMap. Empty map when
    /// no override exists. The `name` field is stripped — callers care
    /// about the Profile fields only.
    Q_INVOKABLE QVariantMap rawProfile(const QString& path) const;

    /// Effective Profile for @p path: walks the parent chain through the
    /// process-wide registry (or user dir as fallback) and fills any
    /// still-missing fields with `Profile::Default*` constants. Always
    /// returns a populated map.
    Q_INVOKABLE QVariantMap resolvedProfile(const QString& path) const;

    /// Write @p profileJson as the user override at @p path. The map
    /// follows `Profile::toJson()` shape (curve / duration / minDistance /
    /// sequenceMode / staggerInterval / presetName); a top-level `name`
    /// field is added automatically. Emits `overrideChanged(path)` on
    /// success. Rejects any @p path that isn't a built-in event path —
    /// path traversal (`../etc/passwd`) and arbitrary names cannot reach
    /// the disk.
    /// @return true on a successful disk write.
    Q_INVOKABLE bool setOverride(const QString& path, const QVariantMap& profileJson);

    /// Delete the override file at @p path. Emits `overrideChanged(path)`
    /// when a file actually existed and was removed. Same path validation
    /// as `setOverride`. @return true when a file was removed.
    Q_INVOKABLE bool clearOverride(const QString& path);

    /// Library of user-saved Profile presets. Each entry is a Profile JSON
    /// (`curve`, `duration`, `name`, …) sitting in the same `profiles/`
    /// dir as overrides — distinguished by the `name` field NOT matching
    /// any `ProfilePaths::` constant. Entries with a `curve` starting
    /// with `"spring:"` are spring presets; everything else is easing.
    Q_INVOKABLE QVariantList userPresets() const;

    /// Save @p profileJson under @p name as a user preset. Rejects names
    /// that collide with a built-in `ProfilePaths::` event path so a
    /// preset can't accidentally shadow an override slot. @return true
    /// on a successful disk write. Emits `userPresetsChanged()`.
    Q_INVOKABLE bool addUserPreset(const QString& name, const QVariantMap& profileJson);

    /// Delete the user preset whose `name` field matches @p name. Will
    /// never delete an override file even when its `name` field happens
    /// to match. @return true on a successful delete. Emits
    /// `userPresetsChanged()`.
    Q_INVOKABLE bool removeUserPreset(const QString& name);

    // ── Motion sets (Phase 7) ────────────────────────────────────────

    /// Lists the user's saved motion-set files. Each row:
    ///   { name, description, overrideCount, slug }
    Q_INVOKABLE QVariantList availableMotionSets() const;

    /// Reads the motion-set file at @p name and writes one per-path
    /// override file for every entry. Atomic: validates every entry
    /// up-front; rejects the whole set on any malformed entry rather
    /// than committing partial state. "Merge" semantics: existing
    /// overrides at paths NOT in the set are preserved. Emits
    /// `overrideChanged()` for each path written and a single
    /// `pendingChangesChanged()` at the end. @return true on
    /// successful read + per-path writes.
    Q_INVOKABLE bool applyMotionSet(const QString& name);

    /// Snapshots the current set of per-path override files into a
    /// motion-set JSON under
    /// `~/.local/share/plasmazones/motionsets/<slug>.json`. @p
    /// description is freeform metadata for the UI; pass an empty
    /// string to omit.
    Q_INVOKABLE bool saveCurrentAsMotionSet(const QString& name, const QString& description);

    /// Delete a saved motion-set file.
    Q_INVOKABLE bool removeMotionSet(const QString& name);

    // ── Shader effects (Phase 6) ─────────────────────────────────────

    /// True when @p path is one of the event paths the daemon's overlay
    /// service actually consumes as a shader-leg surface (osd.show /
    /// .hide and the `popup.<surface>.<show|hide>` family). Other
    /// paths persist a shader assignment but never produce a visible
    /// shader leg — the QML picker hides itself on those rows so the
    /// user gets clear "this control does nothing here" feedback rather
    /// than picking a shader and seeing no change. Single source of
    /// truth lives in @c src/core/animationshadersupportedpaths.h —
    /// adding a new shader-leg surface in the daemon means appending
    /// its leg paths there in lockstep.
    Q_INVOKABLE bool supportsShaderLeg(const QString& path) const;

    /// Installed `AnimationShaderEffect`s flattened to a QML-friendly list.
    /// Each row: id / name / description / author / version / category /
    /// isUserEffect / parameters (QVariantList of ParameterInfo maps).
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// Single-effect lookup. Empty map when @p effectId is unknown.
    Q_INVOKABLE QVariantMap shaderEffectInfo(const QString& effectId) const;

    /// Just the parameters list for @p effectId — convenience for the
    /// per-event shader-param editor.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

    /// XDG-writable user shader directory path (no side effects). Use
    /// `ensureUserShaderDirectory()` if you also need it created on disk.
    /// Internal helper — not exposed to QML; the page surfaces an
    /// "Open Folder" button that calls `openUserShaderDirectory()`
    /// directly rather than displaying the path as a label.
    QString userShaderDirectoryPath() const;

    /// Ensure the user shader directory exists; create it if missing.
    /// @return true when the directory exists (newly created or already
    /// present).
    Q_INVOKABLE bool ensureUserShaderDirectory();

    /// Open the user shader directory in the system file manager,
    /// creating it first if missing.
    Q_INVOKABLE void openUserShaderDirectory();

    /// Install a shader pack from a dropped folder. @p sourceUrl accepts
    /// either a `file://` URL (drag-drop from a file manager) or a bare
    /// absolute path (programmatic callers); both forms are normalised
    /// via `QDir::cleanPath` before use. The source must be a directory
    /// containing a `metadata.json` at its root. The directory is copied
    /// recursively into `userShaderDirectoryPath()/<basename>`; the
    /// registry's filewatcher detects the new pack and emits
    /// `effectsChanged` automatically. Validates that the source exists,
    /// is a non-symlinked directory with a non-symlinked `metadata.json`,
    /// and that the basename does not collide with an existing entry in
    /// the user dir (collision returns false rather than overwriting).
    /// Symlinks anywhere inside the source tree are silently skipped by
    /// the recursive copy. @return true on success.
    Q_INVOKABLE bool installShaderPack(const QString& sourceUrl);

    /// Per-event shader override read.
    /// @return `{ effectId: QString, parameters: QVariantMap }` or empty
    /// when no override is set at this exact path.
    Q_INVOKABLE QVariantMap rawShaderProfile(const QString& path) const;

    /// Walk the parent chain to resolve the effective shader assignment
    /// for @p path. Returns an empty map if no ancestor has one.
    Q_INVOKABLE QVariantMap resolvedShaderProfile(const QString& path) const;

    /// Assign @p effectId (with optional @p parameters) to @p path.
    /// An empty @p effectId clears the assignment at this path,
    /// equivalent to `clearShaderOverride(path)`. Emits
    /// `pendingChangesChanged()` whenever the call actually changed
    /// state.
    Q_INVOKABLE bool setShaderOverride(const QString& path, const QString& effectId, const QVariantMap& parameters);

    /// Remove the shader override at @p path; ancestors take over via
    /// `ShaderProfileTree::resolve` walk-up. Emits
    /// `pendingChangesChanged()` whenever the call actually changed
    /// state.
    Q_INVOKABLE bool clearShaderOverride(const QString& path);

    /// Count of shader overrides on paths strictly DEEPER than @p path
    /// (i.e. paths whose first component up to a `.` matches @p path).
    /// Used by parent-node cards to surface "N deeper overrides shadow
    /// this parent" — without it, a stale leaf set in a previous
    /// session silently wins the deeper-leaf-overlay merge inside
    /// `ShaderProfileTree::resolve` and the parent's value never
    /// reaches runtime even though the UI control shows it set.
    Q_INVOKABLE int shaderOverrideDescendantCount(const QString& path) const;

    /// Clear every shader override whose path is strictly DEEPER than
    /// @p path (i.e. paths starting with `<path>.`). Does NOT clear
    /// the override at @p path itself. Returns the number of cleared
    /// entries. Persists the batch via a single `setShaderProfileTree`
    /// write, which fires `shaderProfileTreeChanged` once and (via the
    /// constructor's broadcast lambda) one path-agnostic
    /// `shaderProfileChanged()` signal — NOT one per cleared path.
    /// Also emits `pendingChangesChanged()`. Used by parent-node
    /// cards' "Clear shadowing children" affordance.
    Q_INVOKABLE int clearShaderOverrideDescendants(const QString& path);

    /// Reverse-lookup: list every event path whose direct shader
    /// override targets @p effectId. Each entry: `{ path, label }` with
    /// @c label produced by @c eventLabel(path). Used by the read-only
    /// shaders browser to surface a "Used in:" line per shader so a
    /// user can tell at a glance which assignments would be affected if
    /// they uninstalled or replaced a pack. Inherited / resolved
    /// references are intentionally NOT included — only direct
    /// overrides count, mirroring the existing tree-walk semantics.
    Q_INVOKABLE QVariantList shaderEffectUsages(const QString& effectId) const;

    /// Test hook: redirect file I/O to @p dir instead of the XDG default.
    /// Pass an empty string to restore the default. Not Q_INVOKABLE — QML
    /// callers must not redirect persistence.
    void setUserProfilesDirOverride(const QString& dir);

Q_SIGNALS:
    /// Emitted on any successful set/clearOverride. @p path is the
    /// affected event path.
    void overrideChanged(const QString& path);

    /// Emitted on any successful add/removeUserPreset.
    void userPresetsChanged();

    /// Emitted on set/clearShaderOverride AND on any full settings
    /// reload (`ISettings::shaderProfileTreeChanged`). The path-agnostic
    /// emission on reload is the cheapest way to refresh every visible
    /// event card after Discard / Settings::load() — diffing the tree
    /// would be more expensive than just rebinding.
    void shaderProfileChanged(const QString& path);

    /// Re-emit of `AnimationShaderRegistry::effectsChanged` so QML can
    /// rebind without poking at the registry directly.
    void shaderEffectsChanged();

    /// Emitted on any successful add/removeMotionSet or apply.
    void motionSetsChanged();

    /// Emitted whenever `hasPendingChanges()` may have flipped. The
    /// SettingsController's slot calls `setNeedsSave(true)` when there
    /// are pending changes; emits with `false`-equivalent state on
    /// commit/revert too so the slot can re-evaluate.
    void pendingChangesChanged();

    /// User-facing transient notification request. QML chrome wires
    /// this to `window.showToast()` so a failed shader-pack install
    /// (or a mutator refused mid-discard) surfaces the underlying
    /// reason instead of returning false silently.
    void toastRequested(const QString& text);

public:
    // ── Save / Discard integration (Phase 8) ─────────────────────────
    //
    // Animation edits write to disk immediately for live preview, but we
    // still want the standard "Discard" button to revert this session's
    // changes. The controller keeps a per-file snapshot of pre-edit
    // content (or "did not exist" sentinel); commit clears it, revert
    // restores files from it. Kept in its own block as the dedicated
    // SettingsController integration surface.

    /// True iff there are unsaved changes the user could still discard.
    bool hasPendingChanges() const;

    /// Forget the snapshot — every change so far is now "saved." Called
    /// from `SettingsController::save()`.
    void commitPending();

    /// Restore every file in the snapshot to its pre-edit state and
    /// clear the snapshot. Called from `SettingsController::load()`
    /// (Discard). Emits `overrideChanged`/`userPresetsChanged`/
    /// `motionSetsChanged`/`shaderProfileChanged` so QML refreshes.
    /// Failures (e.g. permission errors during file restore) are
    /// retained in the snapshot so a subsequent revert can retry.
    void revertPending();

    /// Async sibling of revertPending — runs the QSaveFile restore
    /// loop on a QtConcurrent worker so a Discard with dozens of
    /// snapshotted profile paths doesn't stall the GUI thread for
    /// dozens of disk round-trips. Emits the inherited
    /// `discardResult(ok, error)` signal on completion (back on the
    /// GUI thread) so a future chrome footer can surface
    /// "discarding..." state. Same retain-on-failure semantics as
    /// revertPending — partial failures stay in m_pendingFileSnapshots
    /// for a subsequent retry.
    Q_INVOKABLE void asyncRevertPending();

private:
    QString userProfilesDir() const;
    QString userMotionSetsDir() const;
    QString profileFilePath(const QString& path) const;

    /// True iff @p path is a member of `ProfilePaths::allBuiltInPaths()`.
    /// All filesystem-touching methods reject non-member paths so a
    /// crafted `path` (e.g. `"../etc/passwd"`) cannot escape the
    /// profiles directory.
    bool isValidEventPath(const QString& path) const;

    /// Capture @p filePath's current content into the snapshot if not
    /// already snapshotted. Called by every file-mutating method just
    /// before the write/delete so revert can put it back.
    /// @return @c true if the snapshot is in place after the call (or
    /// was already there); @c false if the file exists but couldn't be
    /// read for snapshot capture (e.g. mid-session permission drift).
    /// Callers that proceed with a write on a @c false return will
    /// permanently lose pre-edit content — the controller's direct
    /// callers (setOverride / clearOverride) bail in that case.
    bool snapshotFileIfFirst(const QString& filePath);

    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    ISettings* m_settings = nullptr;
    QString m_userProfilesDirOverride; ///< Empty = use XDG default

    // Sub-services owned via QObject-parent: both are constructed with
    // `this` as parent so ~AnimationsPageController tears them down
    // automatically. No manual delete; no QPointer needed.
    AnimationPresetLibrary* m_presets = nullptr;
    MotionSetStore* m_motionSets = nullptr;

    /// Pre-edit file contents keyed by absolute path. `std::nullopt`
    /// means "the file did not exist before this session." Mutated only
    /// from the GUI thread. Shader-tree edits don't go through this
    /// snapshot — they ride the standard Settings::load() Q_PROPERTY
    /// re-emit path like every other settings page. During a worker
    /// run, two copies exist briefly (live + captured): the worker
    /// owns a value-captured snapshot, while this member continues to
    /// reflect GUI-thread state. Never alias them by reference —
    /// merging is done by key set in the worker's finished handler.
    QHash<QString, std::optional<QByteArray>> m_pendingFileSnapshots;
    bool m_shaderTreeDirty = false;
    /// In-flight guard for asyncRevertPending — set true on dispatch
    /// and cleared in the QFutureWatcher::finished handler. A second
    /// asyncRevertPending invocation while a worker is running would
    /// run on stale captured state AND the second worker would
    /// overwrite the first's retained map, producing inconsistent
    /// disk state. Mirrors ApplicationController::m_applying.
    bool m_asyncRevertInFlight = false;
    /// Monotonic generation counter bumped on every asyncRevertPending
    /// dispatch. The shaderProfileTreeChanged DirectConnection lambda
    /// captures the value at connect time of each invocation — when an
    /// external reload (Settings::load() chasing discard()) fires
    /// shaderProfileTreeChanged mid-worker, the lambda's captured
    /// generation no longer matches and it MUST skip clearing
    /// `m_shaderTreeDirty`. The worker's finished handler then clears
    /// the dirty bit as part of its terminal sequence.
    quint64 m_asyncRevertGeneration = 0;
    /// Last observed value of hasPendingChanges() seen by the
    /// pendingChangesChanged → dirtyChanged forwarder. CLAUDE.md:
    /// "Only emit signals when value actually changes". Several call
    /// sites emit pendingChangesChanged unconditionally; gating the
    /// forward on this cached state keeps the framework's dirty
    /// Q_PROPERTY NOTIFY contract honest.
    bool m_lastHadPendingChanges = false;
    /// Set to true while a controller-owned setter is mutating the
    /// shader profile tree on m_settings. The shaderProfileTreeChanged
    /// handler checks `m_mutatingShaderTree > 0` to distinguish our own
    /// writes (which keep m_shaderTreeDirty true) from external reloads
    /// (which should clear it). Counter, not bool: a nested re-entrant
    /// write inside the same setShaderProfileTree call chain must not
    /// prematurely clear the outer scope's protection.
    int m_mutatingShaderTree = 0;
    /// Memoised eventSections() result — taxonomy is static for the
    /// process lifetime so subsequent QML rebinds reuse the same list.
    /// Populated lazily on first call. NOTE: if `ProfilePaths::
    /// allBuiltInPaths()` ever becomes dynamic (e.g. plugin-discovered
    /// event paths), this cache needs a clear() trigger — currently
    /// there's nothing to invalidate it because the source is
    /// compile-time static.
    mutable QVariantList m_eventSectionsCache;
};

} // namespace PlasmaZones
