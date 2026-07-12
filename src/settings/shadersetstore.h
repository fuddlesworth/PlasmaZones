// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

namespace PlasmaZones {

/// Persistence + CRUD for shader-set JSON files, shared by the two set
/// domains (motion sets over per-event override files, decoration sets over
/// the surface profile tree). One instance per domain, hosted as a child
/// QObject of that domain's page controller and handed to QML as the
/// `bridge` of ShaderSetsPage.
///
/// The store owns everything domain-agnostic: the sets directory, slugs,
/// atomic writes, listing, apply / save / remove / rename / export / import,
/// and the coverage + "active" summaries. It treats a set's payload as
/// opaque JSON and delegates the three domain-specific steps to injected
/// callables (Config below).
///
/// Both domains happen to serialise the SAME envelope:
/// ```
/// { "name": …, "description": …, "version": 1,
///   "baseline": { … },                       // optional (decoration only)
///   "overrides": [ { "path": …, "profile": { … } }, … ] }
/// ```
/// so coverage (which root sections a set touches) and active-detection are
/// computed generically here rather than per domain.
class ShaderSetStore : public QObject
{
    Q_OBJECT

public:
    /// Absolute path of the directory holding this domain's set files.
    /// Called per operation, never cached, so the controllers' test-only
    /// directory overrides keep working.
    using DirFn = std::function<QString()>;

    /// Capture the current live state as a set payload (the envelope above,
    /// minus name/description/version — the store stamps those). Must be
    /// deterministic: it is compared against saved payloads to decide which
    /// set is active. An empty object means "nothing to save".
    using SnapshotFn = std::function<QJsonObject()>;

    /// Validate a parsed set payload against the domain's path taxonomy.
    /// Whole-set validation: return false if ANY entry is malformed, so a
    /// bad file is refused rather than partially committed / imported.
    using ValidateFn = std::function<bool(const QJsonObject& /*root*/)>;

    /// Commit a validated payload to live state (merge semantics: paths the
    /// set does not cover keep their current values).
    using ApplyFn = std::function<bool(const QJsonObject& /*root*/)>;

    /// Optional pre-write snapshot of a set file, wired to the animations
    /// controller's `snapshotFileIfFirst` so Discard can restore set files
    /// it overwrote. Decoration leaves this null (its writes ride the
    /// normal settings staging flow).
    using FileSnapshotFn = std::function<void(const QString& /*filePath*/)>;

    /// Optional gate consulted before every mutation. Returns an empty
    /// string when the mutation may proceed, or a user-facing refusal
    /// reason (surfaced via toastRequested) when it may not. Wired to the
    /// animations controller's in-flight-discard guard.
    using MutationGuardFn = std::function<QString()>;

    struct Config
    {
        DirFn setsDir;
        SnapshotFn snapshot;
        ValidateFn validate;
        ApplyFn apply;
        FileSnapshotFn fileSnapshot; // optional
        MutationGuardFn mutationGuard; // optional
        /// Current on-disk format. Save stamps it; apply and import refuse a
        /// NEWER file, so a set written by a future build (carrying fields
        /// this build drops on parse) fails cleanly instead of committing a
        /// silently truncated look. A missing version reads as this one.
        int formatVersion = 1;
    };

    explicit ShaderSetStore(Config config, QObject* parent = nullptr);

    /// Saved sets, one row per file:
    /// `{ name, description, slug, coverage: [section…], coverageCount,
    ///    hasBaseline, active }`
    /// `active` is true when every entry the set carries is already live
    /// with an equal profile (containment, not equality — apply merges, so
    /// unrelated live overrides must not clear the badge).
    Q_INVOKABLE QVariantList availableSets() const;

    Q_INVOKABLE bool applySet(const QString& name);
    Q_INVOKABLE bool saveCurrentAsSet(const QString& name, const QString& description);
    Q_INVOKABLE bool removeSet(const QString& name);

    /// Update a set's metadata in place, keeping the payload: rename and/or
    /// change the description (empty clears it). Refuses when @p newName is
    /// empty, slugifies to nothing, or collides with an existing set.
    Q_INVOKABLE bool updateSet(const QString& oldName, const QString& newName, const QString& description);

    /// Copy the named set to @p destLocalPath (a local filesystem path, not
    /// a URL — QML passes it through `settingsController.urlToLocalFile`).
    Q_INVOKABLE bool exportSet(const QString& name, const QString& destLocalPath);

    /// Validate and copy an external set file into the sets directory. The
    /// imported set is renamed to a free name on collision, so an import
    /// never silently overwrites a set the user already has. Accepts either
    /// a local path or a file:// URL (the drop zone hands over raw URLs).
    Q_INVOKABLE bool importSet(const QString& sourcePathOrUrl);

    /// Open (creating if needed) the sets directory in the file manager.
    Q_INVOKABLE void openSetsDirectory();

    /// Absolute path the named set serialises to. Empty when @p setName
    /// slugifies to an empty string.
    Q_INVOKABLE QString setFilePath(const QString& setName) const;

    /// Re-emit setsChanged() so QML re-reads the rows. Wired to each
    /// domain's live-state signal: the `active` flag is derived from live
    /// state, so it goes stale when the user edits a chain elsewhere.
    Q_INVOKABLE void notifyLiveStateChanged();

Q_SIGNALS:
    /// Any change to the saved-set list OR to the live state the `active`
    /// flag is derived from. QML reloads availableSets() on this.
    void setsChanged();

    /// Mirrors the animations controller's staging signal (its set writes
    /// are snapshotted for Discard). Decoration ignores it.
    void pendingChangesChanged();

    /// User-facing failure reason for the chrome toast.
    void toastRequested(const QString& text);

private:
    /// Read + parse a set file. Returns false (and warns) on unreadable or
    /// malformed JSON. @p filePath is the absolute set-file path.
    bool readSetFile(const QString& filePath, QJsonObject* out) const;

    /// Version gate shared by applySet and importSet.
    bool versionAccepted(const QJsonObject& root, const QString& context) const;

    /// True when the mutation may proceed; emits the refusal toast when not.
    bool mutationAllowed();

    /// A free (non-colliding) set name derived from @p desiredName.
    QString uniqueSetName(const QString& desiredName) const;

    Config m_config;
};

} // namespace PlasmaZones
