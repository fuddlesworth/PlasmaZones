// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetStore.h>

#include <QDir>
#include <QLoggingCategory>

#include <array>

namespace PhosphorShaders {

namespace {
Q_LOGGING_CATEGORY(lcPresetStore, "phosphorshaders.presetstore")

/// Every family, in one place, so adding one cannot leave a loader unbuilt.
constexpr std::array<ShaderFamily, 4> kAllFamilies{
    ShaderFamily::Animation,
    ShaderFamily::Surface,
    ShaderFamily::Pointer,
    ShaderFamily::Overlay,
};
} // namespace

ShaderPresetStore::ShaderPresetStore(QObject* parent)
    : QObject(parent)
    , m_registry(new ShaderPresetRegistry(this))
{
}

ShaderPresetStore::~ShaderPresetStore()
{
    // Delete the loaders before the registry, not with it. Both are QObject
    // children of this store, and QObject frees children in insertion order —
    // the registry is constructed in the init list above, so it is child #0 and
    // would die first, leaving each loader's destructor retracting its presets
    // through a freed registry. Reproduced under ASAN as a heap-use-after-free,
    // with the write landing inside QHash::insert.
    //
    // The loaders' QPointer to the registry makes that safe even if this order
    // is ever lost, but the retraction is wanted, so do it while the registry
    // is still alive rather than relying on the null check to skip it.
    qDeleteAll(m_loaders);
    m_loaders.clear();
}

void ShaderPresetStore::load(const QString& root)
{
    // Idempotent by refusal rather than by rebuild. A second call used to build
    // four more loaders, leak the first four with their watchers still armed,
    // and leave two publishers per family — which the loader destructor's
    // whole-family retraction cannot survive, since the first one to die wipes
    // the other's presets.
    if (!m_loaders.isEmpty()) {
        return;
    }

    // An empty root would resolve against the process working directory:
    // QDir(QString()) is QDir("."), whose exists() is true, so the migration
    // would scan and read the CWD's *.json files, and userPresetDirectory()
    // would hand out filesystem-root paths like "/animation". A relative root
    // is the same hazard with a mkpath that succeeds.
    if (root.isEmpty() || !QDir::isAbsolutePath(root)) {
        qCWarning(lcPresetStore) << "Refusing to load presets from a root that is not an absolute path:" << root;
        return;
    }

    m_root = root;

    // Before the scan, or the imported presets would not be seen until the
    // next rescan. Idempotent, so calling it on every startup is free.
    migrateLegacyOverlayPresets(root);

    for (const ShaderFamily family : kAllFamilies) {
        auto* loader = new ShaderPresetLoader(*m_registry, family, this);
        // LiveReload::On is the point of the whole design: a preset retuned on
        // disk has to reach every process without a restart. The watcher
        // promotes itself to the parent directory when the family directory
        // does not exist yet, so a fresh install picks up the first preset the
        // user saves without anyone having to create the tree up front.
        loader->loadFromDirectory(userPresetDirectory(root, family), LiveReload::On);
        m_loaders.insert(static_cast<int>(family), loader);
    }
}

ShaderPresetRegistry& ShaderPresetStore::registry()
{
    return *m_registry;
}

const ShaderPresetRegistry& ShaderPresetStore::registry() const
{
    return *m_registry;
}

ShaderPresetLoader* ShaderPresetStore::loader(ShaderFamily family) const
{
    return m_loaders.value(static_cast<int>(family), nullptr);
}

QString ShaderPresetStore::root() const
{
    return m_root;
}

QString ShaderPresetStore::directoryFor(ShaderFamily family) const
{
    return userPresetDirectory(m_root, family);
}

} // namespace PhosphorShaders
