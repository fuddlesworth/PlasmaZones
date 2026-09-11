// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorShaders/ShaderPreset.h>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorShaders {
class ShaderPresetStore;
}

namespace PlasmaZones {

/**
 * @brief QML-facing CRUD over one shader family's named parameter presets.
 *
 * The write half of the preset feature. `PhosphorShaders::ShaderPresetStore`
 * owns reading and watching; this owns saving, renaming and deleting, and
 * exposes the merged list to QML. One instance per family, handed to QML as
 * the `presetBridge` of whichever editor is showing that family's packs.
 *
 * Shaped after `ShaderSetStore`, its sibling: same atomic write discipline, same
 * "name is not identity" rule, same refusal predicate for a rename dialog to gate
 * its Ok button on. NOT the same on two counts worth knowing — there is no slug
 * step here (a preset's file is named after its id, which is a minted UUID), and
 * each set store is parented to the controller owning its domain while these four
 * are parented to the root controller. The two are different things
 * though, and the distinction is worth keeping straight — a SET is which packs
 * are assigned where across a whole tree, a PRESET is how one pack is tuned.
 * They compose: a set can name assignments that reference presets.
 *
 * ## Identity
 *
 * A preset's id is a UUID minted at save time and never reused. Renaming
 * changes only the display name, so every assignment pointing at the preset
 * keeps working — which is the whole reason assignments store a reference
 * rather than a copy. The FILE is named after the id, so a rename does not
 * move it.
 *
 * ## Pack-declared presets
 *
 * Listed alongside the user's own and marked `readOnly`. They can be duplicated
 * into a user preset but never edited or deleted in place: they belong to the
 * pack, and the next pack update would overwrite the edit anyway.
 */
class ShaderPresetBridge : public QObject
{
    Q_OBJECT

public:
    /// @p store must outlive the bridge.
    ShaderPresetBridge(PhosphorShaders::ShaderPresetStore& store, PhosphorShaders::ShaderFamily family,
                       QObject* parent = nullptr);
    ~ShaderPresetBridge() override;

    /// Every preset offered for @p packId, user presets first, as
    /// `{ id, name, readOnly }` rows. Whether a preset is MODIFIED is not a
    /// property of the preset — it depends on what the asking assignment stores —
    /// so the editor answers that itself from its own delta map.
    Q_INVOKABLE QVariantList presetsFor(const QString& packId) const;

    /// The parameters @p presetId stands for, or an empty map when it names
    /// no preset. QML uses this to show what picking a preset would do, and to
    /// decide which of the live values are deltas on top of it.
    Q_INVOKABLE QVariantMap presetParams(const QString& packId, const QString& presetId) const;

    /// True when @p name is one `savePreset` / `renamePreset` will accept:
    /// non-empty after trimming, within the length cap, and free of control or
    /// formatting characters (the name is rendered in a combo row, where a newline
    /// or a bidi override mangles the row rather than merely looking odd).
    ///
    /// A rename dialog gates its Ok button on this, because an AcceptRole
    /// button dismisses the dialog before the refusal is known. Names are NOT
    /// required to be unique: the id is the identity, and two presets called
    /// "Soft" are a legitimate thing to have while the user decides.
    Q_INVOKABLE bool canUsePresetName(const QString& name) const;

    /// Save @p params for @p packId under @p name as a NEW preset.
    /// @return the new preset's id, or an empty string on refusal (the failure
    ///         signal carries the reason).
    Q_INVOKABLE QString savePreset(const QString& packId, const QString& name, const QVariantMap& params);

    /// Replace @p presetId's parameters with @p params, keeping its name.
    /// This is the action that moves every assignment bound to the preset.
    Q_INVOKABLE bool updatePreset(const QString& presetId, const QVariantMap& params);

    /// Change @p presetId's display name. The id and the file are untouched,
    /// so assignments are unaffected.
    Q_INVOKABLE bool renamePreset(const QString& presetId, const QString& name);

    /// Delete @p presetId.
    ///
    /// Assignments pointing at it are NOT rewritten: each falls back to its own
    /// parameters, which is the look it had before it pointed at a preset. The
    /// alternative — hunting every tree and rule for references — would need
    /// this to reach settings it has no business touching, and would silently
    /// rewrite the user's assignments on a delete they may undo by re-saving.
    Q_INVOKABLE bool deletePreset(const QString& presetId);

    /// Copy a pack-declared preset into an editable user one, so a shipped
    /// tuning can be a starting point. Returns the new preset's id.
    Q_INVOKABLE QString duplicatePreset(const QString& packId, const QString& presetId, const QString& name);

    /// Absolute path of this family's preset directory, for a "show me the
    /// files" affordance.
    Q_INVOKABLE QString presetDirectory() const;

Q_SIGNALS:
    /// The set of presets for @p packId changed. Relayed from the registry, so
    /// it fires for an edit made in another process too.
    void presetsChanged(const QString& packId);

    /// A save, rename or delete was refused. Carries a translated, one-line
    /// reason for the editor to surface.
    void presetWriteFailed(const QString& error);

private:
    /// Write @p preset to its file, atomically, and rescan so the registry
    /// reflects it before this returns.
    bool commit(const PhosphorShaders::ShaderPreset& preset);

    PhosphorShaders::ShaderPresetStore* m_store;
    PhosphorShaders::ShaderFamily m_family;
};

} // namespace PlasmaZones
