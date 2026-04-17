// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configbackends.h"

#include "configdefaults.h"
#include "configmigration.h"
#include "perscreenresolver.h"

#include <memory>

namespace PlasmaZones {

namespace {
/// One resolver instance per process — stateless, shared by every backend
/// created via the factories below.
std::shared_ptr<PerScreenPathResolver> sharedResolver()
{
    static auto r = std::make_shared<PerScreenPathResolver>();
    return r;
}

/// Build a JsonBackend at @p path with PZ's resolver + version stamp applied.
std::unique_ptr<PhosphorConfig::IBackend> makeBackend(const QString& path)
{
    auto backend = std::make_unique<PhosphorConfig::JsonBackend>(path);
    // Anchor the root-group name to PZ's canonical "General" accessor so a
    // future rename in ConfigDefaults doesn't silently decouple writeRootString
    // from purgeStaleKeys' preserved-groups list. JsonBackend defaults to
    // "General" already, so this is currently a no-op but pins the invariant.
    backend->setRootGroupName(ConfigDefaults::generalGroup());
    backend->setPathResolver(sharedResolver());
    backend->setVersionStamp(ConfigDefaults::versionKey(), ConfigSchemaVersion);
    return backend;
}
} // namespace

std::unique_ptr<PhosphorConfig::IBackend> createDefaultConfigBackend()
{
    return makeBackend(ConfigDefaults::configFilePath());
}

std::unique_ptr<PhosphorConfig::IBackend> createSessionBackend()
{
    return makeBackend(ConfigDefaults::sessionFilePath());
}

std::unique_ptr<PhosphorConfig::IBackend> createAssignmentsBackend()
{
    return makeBackend(ConfigDefaults::assignmentsFilePath());
}

std::unique_ptr<PhosphorConfig::QSettingsBackend> createLegacyQSettingsBackend()
{
    return std::make_unique<PhosphorConfig::QSettingsBackend>(ConfigDefaults::legacyConfigFilePath());
}

} // namespace PlasmaZones
