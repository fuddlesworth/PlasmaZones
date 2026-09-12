// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>
#include <PhosphorShaders/phosphorshaders_export.h>

#include <PhosphorFsLoader/DirectoryLoader.h>

#include <QList>
#include <QObject>
#include <QString>

#include <array>
#include <memory>

namespace PhosphorShaders {

using LiveReload = PhosphorFsLoader::LiveReload;

/// The standard root for user-saved shader presets:
/// `<XDG_DATA_HOME>/plasmazones/shader-presets`. One subdirectory per family,
/// named by `shaderFamilyToken`.
///
/// Does NOT create the directory. `WatchedDirectorySet` promotes its watch to
/// the parent when the target does not exist yet, so a preset saved into a
/// fresh install is picked up without a restart and without every reader having
/// to mkdir a directory it only wants to read.
PHOSPHORSHADERS_EXPORT QString standardUserPresetRoot();

/// The directory holding @p family's user presets under @p root.
PHOSPHORSHADERS_EXPORT QString userPresetDirectory(const QString& root, ShaderFamily family);

/**
 * @brief Import pre-existing overlay preset files into the family layout.
 *
 * Before presets became assignable, the zone shader browser saved them through
 * a different path. This moves any it finds at @p root into
 * `<root>/overlay/<id>.json`, deriving each id from the old filename so two
 * processes racing the migration agree on the result rather than importing
 * twice. Idempotent: a file it has already moved is not seen again, and each
 * import clears the reference it applied.
 *
 * @return the number of files imported.
 */
PHOSPHORSHADERS_EXPORT int migrateLegacyOverlayPresets(const QString& root);

/**
 * @brief One per consumer, holding every shader preset for every family it names.
 *
 * One per consumer rather than one per process, and that is a convention, not an
 * invariant: nothing here refuses a second instance. Two would double the
 * directory watchers and parse every preset file twice, which is wasteful rather
 * than wrong, so the three consumers each keep exactly one.
 *
 * The whole preset side of a consumer's wiring: the registry, one publisher per
 * family, and the one-shot import of the pre-existing overlay preset files.
 * Three processes need all of this — the settings app, the daemon and the KWin
 * effect — and none of them differs in how it sets it up, so it lives here
 * rather than three times over.
 *
 * A consumer names the families it actually resolves. The effect resolves
 * animation, surface and pointer presets; the daemon resolves animation, surface
 * and overlay; the settings app shows all four. An unlisted family simply has no
 * user presets, which is the registry's documented miss behaviour.
 *
 * Pack-declared presets are NOT loaded here — they arrive from each family's
 * pack registry, whose reload edge the consumer feeds through
 * `PhosphorShaders::seedPackPresets`. That helper goes through
 * `setPackPresetsForFamily`, which diffs the whole family and so retracts a pack
 * that has gone; the per-pack `setPackPresets` overload cannot, which is why no
 * production consumer uses it. This object owns the user side and
 * the directory layout; it deliberately knows nothing about pack registries,
 * which live in four different libraries.
 *
 * ## Why the scanning lives here and not in a class of its own
 *
 * There used to be a `ShaderPresetLoader` between this and
 * `PhosphorFsLoader::DirectoryLoader`: a forwarder of four methods, owning a
 * parse sink, with a destructor that retracted its whole family from the
 * registry. Three separate teardown faults lived in the seam, including a
 * use-after-free that crashed the daemon on every shutdown, and they had one
 * shared cause — **the invariant "a family has exactly one publisher" had no
 * owner.** The loader ASSUMED it (its retraction was whole-family, correct only
 * under that assumption), the registry did not know about it, and this class was
 * the only one positioned to enforce it and merely happened to.
 *
 * So the publisher is now a slot in a fixed array here, one per family, and the
 * invariant is a property of the data structure rather than of a check: a second
 * publisher for a family is not something this class can be asked to build.
 *
 * And the teardown retraction is GONE rather than relocated. It only ever existed
 * because the loader could not see whether anyone else published for its family,
 * and it only made sense while the registry outlived the loaders. The registry is
 * now a by-value member destroyed with this store, so there is nothing to retract
 * FOR — and emitting `presetsChanged` during teardown was itself a hazard, because
 * a consumer's handler runs until ~QObject severs its connections, which is after
 * its members are already destroyed. That removes the use-after-free with no
 * QPointer anywhere in it, and without trading it for one in the consumer.
 *
 * NOTE for a second store in one process: each carries its own registry, so two
 * of them do not corrupt each other — they just pay for two sets of watchers and
 * parse every saved preset twice. One per consumer is the intent.
 *
 * ## Thread safety
 *
 * GUI-thread only, like the registry it owns.
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
     * @brief Import any legacy overlay presets, then scan the named families.
     *
     * Safe to call from every process that has one of these, and in any order:
     * the migration derives its target ids from the old filenames, so two
     * processes racing it agree on the result instead of importing twice.
     *
     * A family already publishing is left exactly as it is, so a second call
     * cannot produce two publishers for one family — and now cannot even be
     * asked to, because the publisher IS the slot.
     *
     * @param root  The preset root, defaulting to `standardUserPresetRoot()`.
     *              Tests pass a temporary directory. Must be an absolute path —
     *              an empty or relative root would resolve against the process
     *              working directory, and is refused with a warning.
     * @param families  Which families to publish. Empty (the default) means all
     *              four. A consumer that resolves only some of them should name
     *              them: a publisher is not just one startup scan, it holds a
     *              QFileSystemWatcher and RE-PARSES every file in its directory
     *              each time the user saves a preset there. The compositor paid
     *              that on its own thread for the overlay family it cannot
     *              consult at all.
     */
    void load(const QString& root = standardUserPresetRoot(), const QList<ShaderFamily>& families = {});

    /// The registry every consumer resolves through.
    ShaderPresetRegistry& registry();
    const ShaderPresetRegistry& registry() const;

    /// Whether this store publishes @p family, i.e. whether `load()` was called
    /// and named it. False for every family before `load()`.
    bool publishes(ShaderFamily family) const;

    /// Rescan @p family's directory synchronously, so the registry reflects the
    /// new state before this returns.
    ///
    /// This is what the WRITE side wants: after saving a preset file the picker
    /// has to show it in the same turn rather than a debounce later, and the
    /// caller usually wants to select what it just saved.
    ///
    /// @return false when this store does not publish @p family, so a caller
    ///         that saved into a family it never loaded learns that rather than
    ///         silently getting a stale list.
    bool rescanNow(ShaderFamily family);

    /// Where a new user preset for @p family belongs.
    QString directoryFor(ShaderFamily family) const;

private:
    /// The parse sink for one family. Defined in the .cpp: it is an
    /// implementation detail of how this class talks to `DirectoryLoader`, and
    /// exposing it is what made the old seam look like an API.
    class Sink;

    /// One family's publisher: the sink that parses its files and the loader
    /// that watches its directory. Both empty until `load()` names the family.
    struct Publisher
    {
        std::unique_ptr<Sink> sink;
        std::unique_ptr<PhosphorFsLoader::DirectoryLoader> loader;

        bool active() const
        {
            return loader != nullptr;
        }
    };

    /// Index of @p family in `m_publishers`. The enum is contiguous from zero,
    /// which is what lets one slot per family be an array rather than a hash a
    /// second entry could be inserted into.
    ///
    /// In-bounds by construction rather than by inspection: the array is sized by
    /// `ShaderFamilyCount`, which is static_asserted against the enum beside its
    /// declaration, so adding a family grows the array instead of indexing past
    /// its end. That is why this needs no per-caller guard.
    static constexpr std::size_t slotOf(ShaderFamily family)
    {
        return static_cast<std::size_t>(family);
    }

    /// DECLARED FIRST, so reverse member-destruction order destroys it LAST — after
    /// every publisher below. That order is LOAD-BEARING, not belt and braces: the
    /// destructor body resets each publisher loader-then-sink and deliberately
    /// retracts NOTHING (see ~ShaderPresetStore), so nothing else protects the
    /// registry from a sink outliving it. By value rather than a QObject child,
    /// because child destruction order is exactly what produced the use-after-free
    /// this shape removes.
    ShaderPresetRegistry m_registry;

    /// One slot per family, so "two publishers for one family" is not
    /// representable. See the class doc.
    std::array<Publisher, ShaderFamilyCount> m_publishers;

    QString m_root;
};

} // namespace PhosphorShaders
