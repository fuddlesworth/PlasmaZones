// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorRules/Rule.h>

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
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
///   "config": { "Group.Path": { "key": value, … }, … },   // sparse config delta
///   "rules":  { "order": [ "{uuid}", … ],                 // resolved user-rule order
///               "upserts": [ { …Rule… }, … ],             // rules added / changed vs parent
///               "removedIds": [ "{uuid}", … ] }           // parent rules this profile drops
/// }
/// ```
///
/// A sidecar `index.json` (`{ "active": "{uuid}"|null, "order": [ … ] }`) tracks
/// the committed active profile and display order — kept out of config.json so
/// the active pointer never pollutes the diffed settings.
///
/// Profile-file CRUD (create / rename / delete / duplicate / import / export /
/// reparent) writes to disk immediately. Only the ACTIVE POINTER and the applied
/// settings follow the Save footer: activation stages the resolved config and
/// user rules and a staged active id (owned by the controller via the injected
/// closures), which the controller commits on Save (writeActiveId) or reverts on
/// Discard.
///
/// The config half is treated opaquely as key/value JSON. The rules half is an
/// id-keyed delta against the parent-resolved user rule set (managed rules are
/// daemon-owned and never carried); equality ignores the renormalized priority.
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

    /// The current live USER rule subset (non-managed), and a sink that stages a
    /// resolved user rule subset into the settings app's Rules page. Optional —
    /// when unset the profile carries no rules (Phase 1 behaviour / config-only
    /// tests).
    using RulesFn = std::function<QList<PhosphorRules::Rule>()>;
    using ApplyRulesFn = std::function<void(const QList<PhosphorRules::Rule>& /*userRules*/)>;

    struct Config
    {
        DirFn profilesDir;
        ConfigFn currentConfig;
        ConfigFn defaultConfig;
        ApplyConfigFn applyConfig;
        ActiveGetFn stagedActiveId;
        ActiveSetFn setStagedActiveId;
        RulesFn currentUserRules; // optional
        ApplyRulesFn applyUserRules; // optional
        /// On-disk envelope version (the config-schema version the delta keys
        /// belong to). Import/activate refuse a file stamped with a DIFFERENT
        /// version rather than mis-applying keys whose shape moved between
        /// schema versions.
        int formatVersion = 5;
    };

    explicit ProfileStore(Config config, QObject* parent = nullptr);

    /// Saved profiles, one row per file, in DEPTH-FIRST (tree) order:
    /// `{ id, name, description, parentId, parentName, isRoot, depth, active,
    ///    modified, signature }`.
    /// `signature` is a stable hex digest of the profile's RESOLVED config +
    /// user rules — the whole cascade, not just this profile's delta — which
    /// QML renders as a small identicon. Two profiles that resolve to the same
    /// settings therefore carry the same signature.
    /// `depth` is the inheritance depth (0 for a root) for indented rendering.
    /// `active` reflects the controller's STAGED active id. `modified` is true
    /// only on the active row when the live settings/rules have diverged from
    /// what that profile resolves to.
    Q_INVOKABLE QVariantList availableProfiles() const;

    /// True when a profile is active and the current live config or user rules
    /// differ from what that profile resolves to (i.e. the user has edited away
    /// from it). Drives the "modified" marker in the page and switcher.
    Q_INVOKABLE bool activeProfileModified() const;

    /// What @p id overrides relative to its parent, one row per changed LEAF:
    /// `{ segments, before, after }`. `segments` is the humanized path to the
    /// leaf (group segments first, then the key path), which the view nests as a
    /// tree. `before`/`after` are the raw values so QML formats them in the
    /// user's locale. Empty when the profile overrides nothing.
    Q_INVOKABLE QVariantList configChanges(const QString& id) const;

    /// The rule differences @p id introduces relative to its parent, one row
    /// per rule: `{ name, change }` where change is "added", "changed", or
    /// "removed".
    Q_INVOKABLE QVariantList ruleChanges(const QString& id) const;

    /// Rewrite @p id's delta to capture the CURRENT live settings and rules
    /// (recomputed against its parent). Used by "Update profile from current
    /// settings" when the active profile has been edited away from.
    Q_INVOKABLE bool updateProfileFromCurrent(const QString& id);

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
        // Rule delta vs the parent-resolved user rule set (keyed by Rule::id):
        // rules added or semantically changed, ids of parent rules dropped, and
        // the resolved user-rule order.
        QList<PhosphorRules::Rule> ruleUpserts;
        QList<QUuid> ruleRemovedIds;
        QList<QUuid> ruleOrder;
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

    /// Full user rule set for @p id: the parent-resolved set with this profile's
    /// delta applied (drop removedIds, upsert changed rules), reordered per the
    /// profile's stored order.
    QList<PhosphorRules::Rule> resolveRules(const QUuid& id, const QHash<QUuid, Record>& all) const;

    /// Compute @p full's rule delta vs @p base into @p rec (upserts / removedIds
    /// / order). Upserts are rules new to @p full or semantically changed;
    /// equality ignores the renormalized `priority` so re-stamped priorities do
    /// not register as changes.
    static void computeRuleDelta(const QList<PhosphorRules::Rule>& full, const QList<PhosphorRules::Rule>& base,
                                 Record& rec);

    /// Rule equality ignoring `priority` (list-order is carried separately).
    static bool rulesSemanticallyEqual(const PhosphorRules::Rule& a, const PhosphorRules::Rule& b);

    /// True when the staged-active profile's resolved config/rules differ from
    /// the current live values. @p all is the loaded profile map (passed in to
    /// avoid a re-scan). False when there is no active profile.
    bool isActiveModified(const QHash<QUuid, Record>& all) const;

    /// Depth-first display order (roots in sibling order, each followed by its
    /// subtree), with each id's inheritance depth. @p all is the loaded map.
    void depthFirstOrder(const QHash<QUuid, Record>& all, QList<QUuid>& orderOut, QHash<QUuid, int>& depthOut) const;

    /// "Snapping.Behavior.ZoneSpan" → "Snapping › Behavior › Zone span".
    static QStringList humanizeGroupSegments(const QString& group);
    /// "borderWidth" → "Border width".
    static QString humanizeKey(const QString& key);

    /// Walk a changed value into one row per differing LEAF, so a structured
    /// setting (a shader profile tree) enumerates the same way every scalar
    /// setting does instead of collapsing into an opaque blob. @p path
    /// accumulates the humanized key path; @p depth guards runaway nesting.
    /// A trigger object is kept whole — QML renders it as "Alt + Right".
    /// @p identityKey names a field already spent on the row label (an array
    /// element's `path`), which is therefore not repeated as a leaf of its own.
    /// @p segments is the humanized path walked so far, carried as a LIST so the
    /// view can nest shared prefixes instead of repeating a long breadcrumb on
    /// every row.
    static void appendLeafRows(const QStringList& segments, const QJsonValue& before, const QJsonValue& after,
                               int depth, QVariantList& rows, const QString& identityKey = QString());

    /// Stable hex digest of @p id's fully-resolved config + user rules. Keyed on
    /// the whole cascade, so a child that overrides nothing hashes identically
    /// to its parent. QML renders it as an identicon.
    QString profileSignature(const QUuid& id, const QHash<QUuid, Record>& all) const;

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
