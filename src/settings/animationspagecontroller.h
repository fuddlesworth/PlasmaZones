// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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
class AnimationsPageController : public QObject
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
    /// Reserved paths (`ProfilePaths::isReservedPath`) are excluded.
    Q_INVOKABLE QVariantList eventSections() const;

    /// First dotted segment of @p path, or `"global"` when @p path is the
    /// global root. Drives the sidebar grouping.
    Q_INVOKABLE QString sectionForPath(const QString& path) const;

    /// Title-cased label for @p path's last segment (e.g. `"zone.snapIn"`
    /// → `"Snap In"`). Falls back to the segment itself if humanisation
    /// fails. Translation hook lives in QML; this is the raw English
    /// form.
    Q_INVOKABLE QString eventLabel(const QString& path) const;

    /// Wraps `ProfilePaths::parentPath`.
    Q_INVOKABLE QString parentPath(const QString& path) const;

    /// Inheritance chain from @p path up to (but excluding) the empty
    /// root. Useful for "snap → zone → global" breadcrumbs.
    Q_INVOKABLE QStringList parentChain(const QString& path) const;

    /// True iff a user override file exists for @p path.
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
    /// success.
    /// @return true on a successful disk write.
    Q_INVOKABLE bool setOverride(const QString& path, const QVariantMap& profileJson);

    /// Delete the override file at @p path. Emits `overrideChanged(path)`
    /// when a file actually existed and was removed. @return true when a
    /// file was removed.
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

    /// Delete the user preset whose `name` field matches @p name.
    /// @return true on a successful delete. Emits `userPresetsChanged()`.
    Q_INVOKABLE bool removeUserPreset(const QString& name);

    // ── Shader effects (Phase 6) ─────────────────────────────────────

    /// Installed `AnimationShaderEffect`s flattened to a QML-friendly list.
    /// Each row: id / name / description / author / version / category /
    /// isUserEffect / parameters (QVariantList of ParameterInfo maps).
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// Single-effect lookup. Empty map when @p effectId is unknown.
    Q_INVOKABLE QVariantMap shaderEffectInfo(const QString& effectId) const;

    /// Just the parameters list for @p effectId — convenience for the
    /// per-event shader-param editor.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

    /// XDG-writable user shader directory; create if missing.
    Q_INVOKABLE QString userShaderDirectory() const;

    /// Open the user shader directory in the system file manager.
    Q_INVOKABLE void openUserShaderDirectory() const;

    /// Per-event shader override read.
    /// @return `{ effectId: QString, parameters: QVariantMap }` or empty
    /// when no override is set at this exact path.
    Q_INVOKABLE QVariantMap rawShaderProfile(const QString& path) const;

    /// Walk the parent chain to resolve the effective shader assignment
    /// for @p path. Returns an empty map if no ancestor has one.
    Q_INVOKABLE QVariantMap resolvedShaderProfile(const QString& path) const;

    /// Assign @p effectId (with optional @p parameters) to @p path.
    /// An empty @p effectId clears the assignment at this path,
    /// equivalent to `clearShaderOverride(path)`.
    Q_INVOKABLE bool setShaderOverride(const QString& path, const QString& effectId, const QVariantMap& parameters);

    /// Remove the shader override at @p path; ancestors take over via
    /// `ShaderProfileTree::resolve` walk-up.
    Q_INVOKABLE bool clearShaderOverride(const QString& path);

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

    /// Emitted on any successful set/clearShaderOverride.
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

public:
    // ── Motion sets (Phase 7) ────────────────────────────────────────

    /// Lists the user's saved motion-set files. Each row:
    ///   { name, description, overrideCount, slug }
    Q_INVOKABLE QVariantList availableMotionSets() const;

    /// Reads the motion-set file at @p name and writes one per-path
    /// override file for every entry. "Merge" semantics: existing
    /// overrides at paths NOT in the set are preserved. Emits
    /// `overrideChanged()` for each path written. @return true on
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

    // ── Save / Discard integration (Phase 8) ─────────────────────────
    //
    // Animation edits write to disk immediately for live preview, but we
    // still want the standard "Discard" button to revert this session's
    // changes. The controller keeps a per-file snapshot of pre-edit
    // content (or "did not exist" sentinel); commit clears it, revert
    // restores files from it.

    /// True iff there are unsaved changes the user could still discard.
    bool hasPendingChanges() const;

    /// Forget the snapshot — every change so far is now "saved." Called
    /// from `SettingsController::save()`.
    void commitPending();

    /// Restore every file in the snapshot to its pre-edit state and
    /// clear the snapshot. Called from `SettingsController::load()`
    /// (Discard). Emits `overrideChanged`/`userPresetsChanged`/
    /// `motionSetsChanged`/`shaderProfileChanged` so QML refreshes.
    void revertPending();

private:
    QString userProfilesDir() const;
    QString userMotionSetsDir() const;
    QString profileFilePath(const QString& path) const;
    QString presetFilePath(const QString& presetName) const;
    QString motionSetFilePath(const QString& setName) const;

    /// Capture @p filePath's current content into the snapshot if not
    /// already snapshotted. Called by every file-mutating method just
    /// before the write/delete so revert can put it back.
    void snapshotFileIfFirst(const QString& filePath);

    /// Capture the shader tree's current JSON serialization if not
    /// already snapshotted. Symmetric helper for shader edits, which
    /// flow through the single Settings::shaderProfileTree blob rather
    /// than per-file storage.
    void snapshotShaderTreeIfFirst();

    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    ISettings* m_settings = nullptr;
    QString m_userProfilesDirOverride; ///< Empty = use XDG default

    /// Pre-edit file contents keyed by absolute path. `std::nullopt`
    /// means "the file did not exist before this session." Mutated only
    /// from the GUI thread.
    QHash<QString, std::optional<QByteArray>> m_pendingFileSnapshots;

    /// Shader-tree-level snapshot for ShaderProfileTree edits — the
    /// tree is one Q_PROPERTY blob, so a per-file snapshot wouldn't
    /// help. Stored as the raw JSON string for round-trip simplicity.
    /// `std::nullopt` = no shader edits this session.
    std::optional<QString> m_pendingShaderTreeSnapshot;
};

} // namespace PlasmaZones
