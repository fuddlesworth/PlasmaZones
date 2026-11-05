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
std::unique_ptr<PhosphorConfig::JsonBackend> makeBackend(const QString& path)
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
    auto backend = makeBackend(ConfigDefaults::sessionFilePath());
    // Session state is machine-written (the daemon's persistence worker,
    // which already rewrites the file wholesale through writeJsonAtomically,
    // and the config migration), and a factory reset deletes it outright. An
    // unreadable copy holds nothing a user would want kept, and refusing
    // there would only lose the daemon's shutdown save.
    backend->setOverwriteUnreadable(true);
    return backend;
}

} // namespace PlasmaZones
