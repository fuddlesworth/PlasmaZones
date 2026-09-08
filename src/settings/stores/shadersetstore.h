// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "settings/utils/animationfileutils.h"

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
/// atomic writes, listing, apply / save / remove / update / export / import,
/// and the coverage + "active" summaries. It treats a set's payload as
/// opaque JSON and delegates the domain-specific steps to the injected
/// callables in Config below (snapshot / validate / apply, plus the optional
/// file-snapshot and mutation-guard hooks).
///
/// Both domains happen to serialise the SAME envelope:
/// ```
/// { "name": …, "description": …, "version": 1,
///   "overrides": [ { "path": …, "profile": { … } }, … ] }
/// (a "baseline" key, even an empty one, is REFUSED by both domain validators)
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
    /// set is active. A payload carrying no overrides means "nothing to
    /// save", and the save is refused.
    using SnapshotFn = std::function<QJsonObject()>;

    /// Validate a parsed set payload against the domain's path taxonomy.
    /// Whole-set validation: return false if ANY entry is malformed, so a
    /// bad file is refused rather than partially committed / imported.
    using ValidateFn = std::function<bool(const QJsonObject& /*root*/)>;

    /// Commit a validated payload to live state (merge semantics: paths the
    /// set does not cover keep their current values).
    using ApplyFn = std::function<bool(const QJsonObject& /*root*/)>;

    /// True when @p live already holds everything @p setProfile would write —
    /// i.e. applying that one entry would change nothing at its path.
    ///
    /// Optional. The default is exact equality, which is right for a domain
    /// whose `apply` REPLACES the whole profile at a path: after applying,
    /// live equals the set, so anything else means it has not been applied.
    /// That is the decoration domain.
    ///
    /// A domain whose entry has independent HALVES, either of which may be
    /// absent, has to supply one. Motion is that case: an entry carries a
    /// timing half, a pack half, or both, and `apply` writes only the halves
    /// present — a pack-only entry deliberately leaves the path's timing
    /// alone. Under exact equality such an entry can never match a path that
    /// legitimately carries timing the set never mentioned, so the whole set
    /// reads as inactive because of one field it does not own.
    using EntrySatisfiedFn = std::function<bool(const QJsonObject& setProfile, const QJsonObject& live)>;

    struct Config
    {
        DirFn setsDir;
        SnapshotFn snapshot;
        ValidateFn validate;
        ApplyFn apply;
        EntrySatisfiedFn entrySatisfied; // optional, defaults to exact equality
        /// Current on-disk format. Save stamps it; apply and import refuse a
        /// NEWER file, so a set written by a future build (carrying fields
        /// this build drops on parse) fails cleanly instead of committing a
        /// silently truncated look. A missing version reads as this one.
        int formatVersion = 1;
    };

    explicit ShaderSetStore(Config config, QObject* parent = nullptr);

    /// True when @p newName is a name updateSet() will accept: non-empty,
    /// slugifiable to a real filename, and not colliding with a set other than
    /// @p currentName (the set being renamed).
    ///
    /// updateSet's refusal set, in one predicate. QML cannot reproduce the slug
    /// rule, so a rename dialog that gated on "non-empty" alone would leave Ok
    /// enabled for a name the store then refuses, and an AcceptRole button
    /// dismisses the dialog before the refusal is known.
    ///
    /// NOT a gate for the SAVE dialog: saveCurrentAsSet(overwrite=true) accepts a
    /// colliding name on purpose (that is the replace-an-existing-set flow), so
    /// gating Save on this would disable it exactly when the user means to
    /// overwrite.
    Q_INVOKABLE bool canUseSetName(const QString& newName, const QString& currentName = QString()) const;

    /// Saved sets, one row per file:
    /// `{ name, description, slug, coverage: [section…], coverageCount,
    ///    active, modified }`
    /// `active` is true when every entry the set carries is already live
    /// with an equal profile (containment, not equality — apply merges, so
    /// unrelated live overrides must not clear the badge). `modified` is the
    /// set file's mtime.
    Q_INVOKABLE QVariantList availableSets() const;

    Q_INVOKABLE bool applySet(const QString& name);

    /// The name a set is ALREADY stored under, when @p name would land on it,
    /// or an empty string when the name is free. Names collide by slug, so
    /// "My Set" and "my set" are the same file: this returns the stored
    /// spelling so the overwrite confirmation names the set actually at risk
    /// rather than what the user just typed. QML asks before saving (see
    /// saveCurrentAsSet).
    Q_INVOKABLE QString existingSetName(const QString& name) const;

    /// Capture live state as a set.
    ///
    /// Re-saving over an existing name is how the user updates a set after
    /// tweaking their look, so it must stay possible — but it destroys the
    /// stored payload and no Discard brings a set file back — set CRUD is
    /// immediate on every domain. So it requires explicit consent:
    /// @p overwrite defaults to false and the call is REFUSED (with a toast)
    /// when the name is taken. QML checks existingSetName() first and passes
    /// overwrite=true only after the user confirms.
    Q_INVOKABLE bool saveCurrentAsSet(const QString& name, const QString& description, bool overwrite = false);

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

    /// Re-emit setsChanged() so QML re-reads the rows. Wired to each
    /// domain's live-state signal: the `active` flag is derived from live
    /// state, so it goes stale when the user edits a chain elsewhere.
    ///
    /// COALESCED: a bulk revert emits the domain's live-state signal once per
    /// restored path, and each setsChanged makes QML re-run availableSets(),
    /// which re-walks the sets dir AND re-snapshots live state. Emitting per
    /// path would put an O(paths x files) disk walk on the GUI thread. The
    /// emission is therefore deferred to the next event-loop turn and
    /// collapsed into one.
    void notifyLiveStateChanged();

Q_SIGNALS:
    /// Any change to the saved-set list OR to the live state the `active`
    /// flag is derived from. QML reloads availableSets() on this.
    void setsChanged();

    /// User-facing failure reason for the chrome toast.
    void toastRequested(const QString& text);

private:
    /// Absolute path the named set serialises to. Empty when @p setName
    /// slugifies to an empty string.
    QString setFilePath(const QString& setName) const;

    /// The sets directory, or an empty string when no accessor is wired.
    /// Every filesystem entry point routes through here, so a misconfigured
    /// store refuses cleanly instead of calling an empty std::function.
    QString setsDirectory() const;

    /// Read + parse a set file. Returns false (and warns) on an unreadable,
    /// oversized, or malformed file. @p filePath is the absolute set-file
    /// path. importSet feeds this a user-chosen path, so the size cap is a
    /// boundary check: a set is kilobytes, and readAll() on a multi-gigabyte
    /// file would be slurped into memory before the parser could reject it.
    bool readSetFile(const QString& filePath, QJsonObject* out) const;

    /// Version gate shared by applySet and importSet.
    bool versionAccepted(const QJsonObject& root, const QString& context) const;

    /// Atomically write @p root to @p filePath.
    bool writeSetFile(const QString& filePath, const QJsonObject& root);

    /// A free (non-colliding) set name derived from @p desiredName.
    QString uniqueSetName(const QString& desiredName) const;

    /// Largest set file the store will read. A set is a few kilobytes of JSON.
    static constexpr qint64 kMaxSetFileBytes = animfileutil::kMaxJsonFileBytes;

    Config m_config;
    /// Guards the coalesced notifyLiveStateChanged() emission (see there).
    bool m_liveStateNotifyQueued = false;
};

} // namespace PlasmaZones
