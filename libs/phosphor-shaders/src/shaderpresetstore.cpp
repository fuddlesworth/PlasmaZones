// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetStore.h>

#include <array>

namespace PhosphorShaders {

namespace {
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

ShaderPresetStore::~ShaderPresetStore() = default;

void ShaderPresetStore::load(const QString& root)
{
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
