// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Not forward-declared: moc needs the complete type to register the
// ShaderSetStore* Q_PROPERTY below as a pointer meta-type.
#include "shadersetstore.h"

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
/// QObjects: `AnimationPresetLibrary` (preset CRUD) and `ShaderSetStore`
/// (motion-set CRUD). The preset library's signals are forwarded to the
/// controller's own signals via `connect()` so QML rebinds without poking
/// at it directly. The set store is the exception: QML binds it straight
/// as `setsBridge` (the shared ShaderSetsPage talks to it), so only its
/// `pendingChangesChanged` is forwarded, for the dirty flag.
class AnimationsPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(qreal springOmegaMin READ springOmegaMin CONSTANT)
    Q_PROPERTY(qreal springOmegaMax READ springOmegaMax CONSTANT)
    Q_PROPERTY(qreal springZetaMin READ springZetaMin CONSTANT)
    Q_PROPERTY(qreal springZetaMax READ springZetaMax CONSTANT)

    /// The motion-set store, bound by AnimationsMotionSetsPage as its `bridge`.
    Q_PROPERTY(PlasmaZones::ShaderSetStore* setsBridge READ setsBridge CONSTANT)

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

    // Slider bounds for the spring editor. These are a deliberately narrower,
    // usable subset of the engine's accepted clamp range (PhosphorAnimation::
    // Spring clamps omega to [0.1, 200] and zeta to [0, 10]). The slider only
    // needs to cover values a user can perceive: above omega ~40 the spring
    // settles in well under ~75 ms (visually instant), and zeta > ~4 is a
    // barely-moving overdamped crawl. zeta is floored at 0.1 (not 0) so the
    // edited spring always settles rather than oscillating forever. A
    // hand-edited config outside this band still parses — the engine clamp,
    // not the slider, is the validity boundary.
    qreal springOmegaMin() const
    {
        return 1.0;
    }
    qreal springOmegaMax() const
    {
        return 40.0;
    }
    qreal springZetaMin() const
    {
        return 0.1;
    }
    qreal springZetaMax() const
    {
        return 4.0;
    }

    /// Built-in event paths, grouped by section. Each entry:
    /// ```
    /// { "section": "window", "label": "Window",
    ///   "paths": [ { "path": "window", "label": "Window (inherited)",
    ///                "parent": "global", "isCategory": true },
    ///              { "path": "window.appearance.open", "label": "Open",
    ///                "parent": "window.appearance", "isCategory": false }, ... ] }
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

    /// Clear every per-event override file (each built-in event path falls back
    /// to its built-in default). Backs the settings app's per-page "Reset to
    /// defaults" for the animation pages: each cleared file is snapshotted like
    /// a normal edit, so the change stages and a subsequent Discard restores it.
    /// The shader tree, animation Profile blob, and window filtering are separate
    /// Settings keys the caller resets alongside this.
    /// @return the number of override files actually removed, or -1 when the
    /// reset did not complete: either it was REFUSED because an async discard
    /// owns the snapshot map (every override file is still on disk), or some
    /// files could not be removed (a partial reset). The page toasts the reason
    /// in both cases. A caller must not treat -1 as "nothing to clear".
    int clearAllOverrides();

    /// Scoped sibling of clearAllOverrides for the per-page kebab: clear only the
    /// override files at @p eventPaths (one settings page's own event-path
    /// subtree), leaving every other page's overrides untouched. Same snapshot /
    /// return-code contract as clearAllOverrides (-1 = refused or partial). @p
    /// eventPaths must be built-in event paths; non-built-in entries are skipped.
    int clearOverridesUnder(const QStringList& eventPaths);

    /// Scoped sibling of revertPending: restore ONLY the snapshotted override
    /// files at @p eventPaths from their pre-edit content, leaving every other
    /// page's staged file edits (and any preset / motion-set snapshots) pending.
    /// Refuses (returns false) while an async discard owns the snapshot map. The
    /// caller reverts the shader tree for the same paths separately (the tree is
    /// Settings-owned; see the revertPending() caller contract). @return true
    /// when every in-scope snapshot restored (or there were none).
    bool revertPendingUnder(const QStringList& eventPaths);

    /// True iff any of @p eventPaths carries a staged (snapshotted) override-file
    /// edit — the file half of a per-page dirty check. The shader-tree half is a
    /// value comparison the caller runs against committedShaderProfileTree().
    bool hasScopedPendingFiles(const QStringList& eventPaths) const;

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

    // ── Motion sets ──────────────────────────────────────────────────

    /// The motion-set store — the `bridge` ShaderSetsPage binds to.
    /// Motion sets snapshot the per-event override FILES under
    /// `~/.local/share/plasmazones/motionsets/<slug>.json`. Applying
    /// merges: overrides at paths NOT in the set are preserved. Writes ride
    /// this controller's `setOverride`, so each one snapshots pre-edit
    /// content and Discard restores it. The domain closures live in
    /// motionsetdomain.cpp.
    ShaderSetStore* setsBridge() const
    {
        return m_motionSets;
    }

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
    /// Each row mirrors `animations_controller_detail::effectToMap`:
    /// id / name / description / author / version / category / appliesTo
    /// (QStringList of event-class tokens, empty = universal) / isUserEffect /
    /// previewPath / parameters (QVariantList of ParameterInfo maps).
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// Path-aware variant of @c availableShaderEffects: the same rows, but
    /// FILTERED to the effects whose declared `appliesTo` class can drive the
    /// event @p path. An effect that can't (e.g. the geometry-only window-morph
    /// on a window.appearance.open row, or a desktop effect on a window row) is
    /// omitted from the returned list, so the per-event shader picker only offers
    /// compatible shaders. Each row still carries `dimmed` (always false) and
    /// `dimReason` (always empty) for QML binding compatibility with @c
    /// availableShaderEffects consumers.
    Q_INVOKABLE QVariantList availableShaderEffectsForPath(const QString& path) const;

    /// Just the parameters list for @p effectId — convenience for the
    /// per-event shader-param editor.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

    /// XDG-writable user shader directory path (no side effects). The directory
    /// is created on demand by openUserShaderDirectory() and by the pack
    /// installer, so a caller that only needs the path pays for no filesystem
    /// write.
    /// Internal helper — not exposed to QML; the page surfaces an
    /// "Open Folder" button that calls `openUserShaderDirectory()`
    /// directly rather than displaying the path as a label.
    QString userShaderDirectoryPath() const;

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
    /// An empty @p effectId writes an explicit "no effect" sentinel at this
    /// path, which BLOCKS inheritance from every ancestor for it and its
    /// descendants. That is deliberately the opposite of
    /// `clearShaderOverride(path)`, which removes the entry so resolution
    /// falls through to the parent again — the sentinel is what makes "I
    /// disabled all popups" stick even when a parent assigns a shader. Emits
    /// `pendingChangesChanged()` whenever the call actually changed state.
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
    /// entries (0 = nothing to clear), or -1 when refused while an
    /// async discard owns the tree (the page toasts the reason).
    /// Persists the batch via a single `setShaderProfileTree`
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

    /// Forget the snapshot — every change so far is now "saved." Called from
    /// apply(); SettingsController::save() deliberately does NOT call it (that
    /// would double-dispatch, see settingscontroller_lifecycle.cpp).
    void commitPending();

    /// Re-evaluate the value-based dirty state after an external commit point the
    /// controller cannot observe on its own — chiefly Settings::save()'s
    /// captureBaseline, which advances committedShaderProfileTree() without moving
    /// the live tree (so no shaderProfileTreeChanged fires). SettingsController::
    /// save() calls this right after m_settings.save(); the dirtyChanged forwarder
    /// gates on an actual flip so a no-op refresh is free.
    void refreshDirtyState();

    /// Restore every file in the snapshot to its pre-edit state and
    /// clear the snapshot. Called from `SettingsController::load()`
    /// (Discard). Emits `overrideChanged`/`userPresetsChanged`/
    /// `pendingChangesChanged` and refreshes the set store through
    /// `ShaderSetStore::notifyLiveStateChanged()` so QML refreshes.
    /// (`shaderProfileChanged` arrives separately, from the caller's
    /// own `Settings::load()`.)
    /// Failures (e.g. permission errors during file restore) are
    /// retained in the snapshot so a subsequent revert can retry.
    /// @return true when the page is CLEAN afterwards. False has two causes, and
    /// a caller that goes on to declare the state clean (an import, a defaults
    /// reset) must honour both, or it strands pre-edit snapshots that a later
    /// Discard writes back over the new state:
    ///   * the revert was REFUSED outright, because an asyncRevertPending() worker
    ///     holds the snapshot map, or
    ///   * some file could not be restored (permission drift) and was RETAINED for
    ///     a retry.
    /// The Discard path itself may ignore the result: ApplicationController's
    /// discardAllAsync dispatches this page's async revert before the settings
    /// domain calls load(), so hitting the guard there means the async worker
    /// already owns the restore.
    bool revertPending();

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

    /// True while an asyncRevertPending() worker owns the snapshot map. A caller
    /// that gets false from revertPending() uses this to tell the two causes
    /// apart: the worker is mid-restore and will finish the job (and re-dirty the
    /// page itself if it has to retain a file), versus a restore that actually
    /// failed and left the page dirty.
    bool asyncRevertInFlight() const;

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
    /// Undo a snapshotFileIfFirst() staging when the write it was taken for
    /// never landed. No-op unless the file on disk still matches the staged
    /// content exactly, so a snapshot guarding an earlier successful edit is
    /// left alone. Handed to the sub-services as a callable.
    /// @return true when the entry was dropped (and pendingChangesChanged was
    /// emitted if that flipped hasPendingChanges()).
    bool dropFileSnapshotIfUnchanged(const QString& filePath);

    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    ISettings* m_settings = nullptr;
    QString m_userProfilesDirOverride; ///< Empty = use XDG default

    // Sub-services owned via QObject-parent: both are constructed with
    // `this` as parent so ~AnimationsPageController tears them down
    // automatically. No manual delete; no QPointer needed.
    AnimationPresetLibrary* m_presets = nullptr;
    ShaderSetStore* m_motionSets = nullptr;

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
    /// In-flight guard for asyncRevertPending — set true on dispatch
    /// and cleared in the QFutureWatcher::finished handler. A second
    /// asyncRevertPending invocation while a worker is running would
    /// run on stale captured state AND the second worker would
    /// overwrite the first's retained map, producing inconsistent
    /// disk state. Mirrors ApplicationController::m_applying.
    bool m_asyncRevertInFlight = false;
    /// Last observed value of hasPendingChanges() seen by the
    /// pendingChangesChanged → dirtyChanged forwarder. CLAUDE.md:
    /// "Only emit signals when value actually changes". Several call
    /// sites emit pendingChangesChanged unconditionally; gating the
    /// forward on this cached state keeps the framework's dirty
    /// Q_PROPERTY NOTIFY contract honest.
    bool m_lastHadPendingChanges = false;
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
