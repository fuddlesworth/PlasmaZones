// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderPresetLoader.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>
#include <PhosphorShaders/phosphorshaders_export.h>

#include <QHash>
#include <QObject>
#include <QString>

namespace PhosphorShaders {

/**
 * @brief One object per process holding every shader preset, for every family.
 *
 * The whole preset side of a consumer's wiring: the registry, one loader per
 * family, and the one-shot import of the pre-existing overlay preset files.
 * Three processes need all of this — the settings app, the daemon and the KWin
 * effect — and none of them differs in how it sets it up, so it lives here
 * rather than three times over.
 *
 * A consumer wires the parts it actually has. The effect resolves animation,
 * surface and pointer presets; the daemon resolves animation, surface and
 * overlay; the settings app shows all four. Loading a family a given process
 * never resolves costs one scan of a usually-absent directory, which is far
 * cheaper than making each process reason about which families it needs.
 *
 * Pack-declared presets are NOT loaded here — they arrive from each family's
 * pack registry, whose reload edge the consumer connects to
 * `ShaderPresetRegistry::setPackPresets`. This object owns the user side and
 * the directory layout; it deliberately knows nothing about pack registries,
 * which live in four different libraries.
 *
 * ## Thread safety
 *
 * GUI-thread only, like the registry and loaders it owns.
 */
class PHOSPHORSHADERS_EXPORT ShaderPresetStore : public QObject
{
    Q_OBJECT

public:
    explicit ShaderPresetStore(QObject* parent = nullptr);
    ~ShaderPresetStore() override;

    ShaderPresetStore(const ShaderPresetStore&) = delete;
    ShaderPresetStore& operator=(const ShaderPresetStore&) = delete;

    /**
     * @brief Import any legacy overlay presets, then scan every family.
     *
     * Safe to call from every process that has one of these, and in any order:
     * the migration derives its target ids from the old filenames, so two
     * processes racing it agree on the result instead of importing twice.
     *
     * Calling it twice on the same store is a no-op. The second call does NOT
     * rebuild: one loader per family is an invariant the loaders depend on,
     * because their teardown retracts a whole family rather than only what they
     * themselves published.
     *
     * @param root  The preset root, defaulting to `standardUserPresetRoot()`.
     *              Tests pass a temporary directory. Must be an absolute path —
     *              an empty or relative root would resolve against the process
     *              working directory, and is refused with a warning.
     */
    void load(const QString& root = standardUserPresetRoot());

    /// The registry every consumer resolves through.
    ShaderPresetRegistry& registry();
    const ShaderPresetRegistry& registry() const;

    /// The loader for @p family, for the write side to rescan after saving.
    /// Null before `load()`.
    ShaderPresetLoader* loader(ShaderFamily family) const;

    /// Where a new user preset for @p family belongs.
    QString directoryFor(ShaderFamily family) const;

private:
    // Parent-based ownership: both are QObjects parented to this, so Qt frees
    // them with it. No smart pointer on top, which would be a second owner for
    // the same object.
    //
    // The destructor deletes m_loaders explicitly first. Qt's own child
    // destruction runs in insertion order, which would free this registry —
    // child #0, built in the init list — before the loaders whose teardown
    // still talks to it.
    ShaderPresetRegistry* m_registry;
    QHash<int, ShaderPresetLoader*> m_loaders;
    QString m_root;
};

} // namespace PhosphorShaders
