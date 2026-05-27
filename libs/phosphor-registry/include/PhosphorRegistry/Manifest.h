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
//
// CMake mirrors this value via the PHOSPHOR_PLUGIN_ABI_VERSION
// compile definition (PUBLIC on the PhosphorRegistry target), so
// test fixture manifests can be templated by configure_file with
// the same @PHOSPHOR_PLUGIN_ABI_VERSION@ substitution and never
// drift from the C++ header. The static_assert below catches skew
// at build time for any translation unit that links
// PhosphorRegistry (the canonical consumer path); a TU including
// this header without linking the target will silently skip the
// check, which is acceptable because such TUs cannot ship a real
// plugin anyway.
constexpr int PluginAbiVersion = 1;

#ifdef PHOSPHOR_PLUGIN_ABI_VERSION
static_assert(PHOSPHOR_PLUGIN_ABI_VERSION == PluginAbiVersion,
              "CMake's PHOSPHOR_PLUGIN_ABI_VERSION must match Manifest.h's PluginAbiVersion");
#endif

// Maximum size in bytes of a manifest.json file the loader will
// accept. Defends against a corrupt or hostile manifest ballooning
// process RSS — a legitimate phosphor plugin manifest is well under
// 1 KiB; the 64 KiB cap tolerates unusually verbose capability lists
// and future schema fields. Exposed publicly so test_manifest can
// assert against the exact boundary without duplicating the literal.
constexpr qint64 ManifestMaxBytes = 64 * 1024;

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
    // ABI version the plugin was built against. Default-initialized
    // to 0 — that is NOT a sentinel for "unset" (a manifest declaring
    // "abi": 0 would also produce 0). Always check isValid first; if
    // isValid is true, abi equals PluginAbiVersion by construction
    // (the parser refuses any other value).
    int abi = 0;
    // Capability declarations. Phase 5 will enforce these against a
    // sandbox; today they are informational and exposed via
    // IFactoryBase::capabilities() on the registered factory.
    QStringList capabilities;
    // Absolute path to the plugin's .so. Populated by the loader,
    // not by the JSON itself — the JSON only carries metadata.
    QString libraryPath;
    // Absolute path to the manifest.json file. Useful for diagnostic
    // logging. Note: Phase 1.3 hot-reload triggers on directory
    // add/remove events from WatchedDirectorySet, not on per-file
    // mtime comparisons — this field is informational only.
    QString manifestPath;
    // False when parse() rejected the input; isValid implies every
    // required field above is populated and abi matched.
    bool isValid = false;
    // When isValid == false, a one-line diagnostic explaining why
    // (missing field, ABI mismatch, malformed JSON). Logged by the
    // loader; not surfaced to end users.
    QString parseError;

    // Parse a manifest.json from disk. pluginDir is the directory
    // the manifest lives in; when non-empty, the manifest's id must
    // match pluginDir's basename. The PluginLoader always passes
    // absolute paths (built via QDir::absoluteFilePath); production
    // callers should do the same. An empty pluginDir skips the
    // directory-basename check — that's the in-memory parseObject
    // test seam, retained on the file-based entry point for
    // symmetry, and is the only way to call parse() without
    // staging a real directory. On parse error, returns a Manifest
    // with isValid == false and parseError populated.
    [[nodiscard]] static Manifest parse(const QString& manifestJsonPath, const QString& pluginDir);

    // Test seam: parse a manifest from an in-memory QJsonObject
    // (skips the file-read step). Used by test_manifest.cpp.
    // pluginDir follows the same semantics as parse() — empty
    // skips the dir-basename check.
    [[nodiscard]] static Manifest parseObject(const QJsonObject& obj, const QString& pluginDir);
};

} // namespace PhosphorRegistry
