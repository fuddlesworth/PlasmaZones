// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PhosphorRegistry {

// Current plugin ABI version. Bumped whenever the C entry-point
// signature, the registry's factory interface vtable layout, or the
// manifest schema changes in a non-backwards-compatible way.
// Plugins whose manifest declares a different abi are refused at
// load time.
constexpr int kPluginAbiVersion = 1;

// Plain-old-data mirror of the plugin manifest.json schema. Filled
// in by Manifest::parse() from the on-disk JSON. The struct stays
// validity-checked via the isValid bool — callers should refuse to
// use a Manifest with isValid == false.
struct PHOSPHORREGISTRY_EXPORT Manifest
{
    // Stable identifier for the plugin. Must match the directory
    // basename the manifest lives in (we enforce this at load time
    // so the on-disk layout and the registered id stay aligned).
    QString id;
    // Human-readable label shown in plugin browsers and settings.
    QString displayName;
    // ABI version the plugin was built against. Loaded only if this
    // equals kPluginAbiVersion.
    int abi = 0;
    // Capability declarations. Phase 5 will enforce these against a
    // sandbox; today they are informational and exposed via
    // IFactoryBase::capabilities() on the registered factory.
    QStringList capabilities;
    // Absolute path to the plugin's .so. Populated by the loader,
    // not by the JSON itself — the JSON only carries metadata.
    QString libraryPath;
    // Absolute path to the manifest.json file. Useful for diagnostic
    // logging and for hot-reload comparisons (mtime tracking).
    QString manifestPath;
    // False when parse() rejected the input; isValid implies every
    // required field above is populated and abi matched.
    bool isValid = false;
    // When isValid == false, a one-line diagnostic explaining why
    // (missing field, ABI mismatch, malformed JSON). Logged by the
    // loader; not surfaced to end users.
    QString parseError;

    // Parse a manifest.json from disk. The pluginDir is the directory
    // the manifest lives in; the manifest's id must match
    // pluginDir's basename. On parse error, returns a Manifest with
    // isValid == false and parseError populated.
    [[nodiscard]] static Manifest parse(const QString& manifestJsonPath, const QString& pluginDir);

    // Test seam: parse a manifest from an in-memory QJsonObject
    // (skips the file-read step). Used by test_manifest.cpp.
    [[nodiscard]] static Manifest parseObject(const QJsonObject& obj, const QString& pluginDir);
};

} // namespace PhosphorRegistry
