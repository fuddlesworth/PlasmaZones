// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>
#include <QVariantList>

#include <functional>

namespace PlasmaZones {

/// Persistence, CRUD, and inheritance resolution for settings profiles. One
/// instance, hosted as a child QObject of ProfilePageController and handed to
/// QML as the `profilesBridge`.
///
/// A profile is a single JSON file `<uuid>.json` under
/// `~/.config/plasmazones/profiles`, holding only what CHANGES from its parent
/// (another profile, or the schema defaults at the root). Profiles form an
/// inheritance tree; resolving a profile walks the chain root → … → profile,
/// overlaying each delta onto the schema-defaults blob to produce the full
/// config that activating it stages into the settings app's Save footer.
///
/// Envelope:
/// ```
/// { "_version": 5, "id": "{uuid}", "name": …, "description": …,
///   "parent": "{uuid}" | null,
///   "config": { "Group.Path": { "key": value, … }, … },   // sparse delta
///   "rules":  { "order": [ "{uuid}", … ], "rules": [ … ] } // Phase 2
/// }
/// ```
///
/// A sidecar `index.json` (`{ "active": "{uuid}"|null, "order": [ … ] }`) tracks
/// the committed active profile and display order — kept out of config.json so
/// the active pointer never pollutes the diffed settings.
///
/// Profile-file CRUD (create / rename / delete / duplicate / import / export /
/// reparent) writes to disk immediately. Only the ACTIVE POINTER and the applied
/// settings follow the Save footer: activation stages the resolved config and a
/// staged active id (both owned by the controller via the injected closures),
/// which the controller commits on Save (writeActiveId) or reverts on Discard.
///
/// The config half is treated opaquely as key/value JSON. The `rules` section is
/// carried through read/write untouched in Phase 1; Phase 2 wires its diff/apply.
class ProfileStore : public QObject
{
    Q_OBJECT

public:
    /// Absolute path of the profiles directory. Called per operation, never
    /// cached, so a test-only directory override keeps working.
    using DirFn = std::function<QString()>;

    /// The complete current config as one JSON blob (live in-memory values,
    /// including unsaved edits) and the schema-defaults blob. Both stamp
    /// `_version`, so a resolved blob round-trips through the config store.
    using ConfigFn = std::function<QJsonObject()>;

    /// Stage a fully-resolved config blob into the settings store WITHOUT
    /// committing to disk (Settings::applyConfigOverlayStaged), so the footer
    /// lights and the user's Save commits it.
    using ApplyConfigFn = std::function<void(const QJsonObject& /*fullConfigBlob*/)>;

    /// Read / write the controller's STAGED active-profile id (the id shown as
    /// active in the list, reverted on Discard). Distinct from the committed
    /// pointer in index.json, which the controller persists on Save.
    using ActiveGetFn = std::function<QString()>;
    using ActiveSetFn = std::function<void(const QString& /*id*/)>;

    struct Config
    {
        DirFn profilesDir;
        ConfigFn currentConfig;
        ConfigFn defaultConfig;
        ApplyConfigFn applyConfig;
        ActiveGetFn stagedActiveId;
        ActiveSetFn setStagedActiveId;
        /// On-disk envelope version (the config-schema version the delta keys
        /// belong to). Import/activate refuse a file stamped with a DIFFERENT
        /// version rather than mis-applying keys whose shape moved between
        /// schema versions.
        int formatVersion = 5;
    };

    explicit ProfileStore(Config config, QObject* parent = nullptr);

    /// Saved profiles, one row per file, in display order:
    /// `{ id, name, description, parentId, parentName, isRoot, active }`.
    /// `active` reflects the controller's STAGED active id.
    Q_INVOKABLE QVariantList availableProfiles() const;

    /// Capture the CURRENT live config as a new profile whose delta is computed
    /// against @p parentId's resolved config (empty @p parentId = root, delta vs
    /// schema defaults). Returns the new profile's id (braced) or an empty
    /// string on failure. The new profile becomes the staged active profile
    /// (current settings already match it, so nothing is applied).
    Q_INVOKABLE QString createProfile(const QString& name, const QString& description, const QString& parentId);

    /// Rename and/or re-describe a profile in place (empty @p description
    /// clears it). Refuses an empty/whitespace name.
    Q_INVOKABLE bool renameProfile(const QString& id, const QString& newName, const QString& description);

    /// Copy a profile (same parent + delta) under a free name. Returns the new id.
    Q_INVOKABLE QString duplicateProfile(const QString& id);

    /// Reparent @p id under @p parentId (empty = root), re-flattening its delta
    /// so its RESOLVED config is unchanged. Refuses a cycle (making a profile a
    /// descendant of itself).
    Q_INVOKABLE bool setParent(const QString& id, const QString& parentId);

    /// Delete a profile. Its children are rebound to its parent (the deleted
    /// node's grandparent for them), re-flattening their deltas so their
    /// resolved config is unchanged. If the deleted profile was the staged
    /// active one, the staged active id is cleared.
    Q_INVOKABLE bool removeProfile(const QString& id);

    /// Resolve @p id's full config and stage it via the applyConfig closure,
    /// and set the staged active id to @p id. The user's Save commits both.
    Q_INVOKABLE bool activateProfile(const QString& id);

    /// Copy the profile file to @p destLocalPath (a local path, not a URL —
    /// QML converts via settingsController.urlToLocalFile).
    Q_INVOKABLE bool exportProfile(const QString& id, const QString& destLocalPath);

    /// Validate and copy an external profile file into the profiles directory,
    /// under a FRESH id (so an import never overwrites an existing profile) and
    /// a free name. Accepts a local path or a file:// URL. If the imported
    /// profile names a parent id that is not present, it is imported as a root.
    /// Returns the new id or an empty string on failure.
    Q_INVOKABLE QString importProfile(const QString& sourcePathOrUrl);

    /// Open (creating if needed) the profiles directory in the file manager.
    Q_INVOKABLE void openProfilesDirectory();

    /// The STAGED active profile id (what the list badges), via the closure.
    Q_INVOKABLE QString activeProfileId() const;

    /// Re-emit profilesChanged() so QML re-reads availableProfiles(). Used when
    /// the staged active id changes outside a store mutation (e.g. the
    /// controller reverting it on Discard).
    void refresh();

    // ── Controller hooks for the Save-footer active-pointer lifecycle ────────
    /// The COMMITTED active id from index.json (the controller's Discard target).
    QString committedActiveId() const;
    /// Persist @p id as the committed active id in index.json (called on Save).
    void writeActiveId(const QString& id);

Q_SIGNALS:
    /// Any change to the saved-profile list or the staged active id. QML
    /// reloads availableProfiles() on this.
    void profilesChanged();

    /// User-facing failure reason for the chrome toast.
    void toastRequested(const QString& text);

private:
    struct Record
    {
        QUuid id;
        QString name;
        QString description;
        QUuid parent; // null → root
        QJsonObject configDelta; // sparse { group: { key: value } }
        QJsonObject rulesSection; // carried verbatim in Phase 1
    };

    QString profilesDirectory() const;
    QString profileFilePath(const QUuid& id) const;
    QString indexFilePath() const;

    /// Read every `<uuid>.json` in the directory into a map keyed by id. A
    /// malformed or version-mismatched file is skipped with a warning.
    QHash<QUuid, Record> loadAll() const;
    bool readProfileFile(const QString& path, Record* out) const;
    bool writeProfileRecord(const Record& rec);
    static QJsonObject recordToJson(const Record& rec, int formatVersion);

    /// Full config for @p id: schema defaults with each ancestor delta (root →
    /// … → id) overlaid. Carries `_version`, so it round-trips through the store.
    QJsonObject resolveConfig(const QUuid& id, const QHash<QUuid, Record>& all) const;
    /// Overlay @p delta's groups/keys onto @p base in place.
    static void overlayConfig(QJsonObject& base, const QJsonObject& delta);
    /// Sparse delta of @p full vs @p base: keys where they differ. Skips the
    /// top-level `_version` marker (only group objects are walked).
    static QJsonObject diffConfig(const QJsonObject& full, const QJsonObject& base);

    /// True when @p maybeAncestor is @p id or one of its ancestors.
    bool isSelfOrAncestor(const QUuid& maybeAncestor, const QUuid& id, const QHash<QUuid, Record>& all) const;

    /// A free display name derived from @p desired (appending " (2)", …),
    /// ignoring the profile @p excludeId (the one being renamed).
    QString uniqueName(const QString& desired, const QHash<QUuid, Record>& all, const QUuid& excludeId = QUuid()) const;

    QJsonObject readIndex() const;
    void writeIndex(const QJsonObject& index);
    QList<QUuid> readOrder() const;
    void appendToOrder(const QUuid& id);
    void removeFromOrder(const QUuid& id);

    /// Largest profile file the store will read (a profile is a few kilobytes).
    static constexpr qint64 kMaxProfileFileBytes = 4 * 1024 * 1024;

    Config m_config;
};

} // namespace PlasmaZones
