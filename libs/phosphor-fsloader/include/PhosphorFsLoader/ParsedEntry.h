// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QString>

#include <any>

namespace PhosphorFsLoader {

/**
 * @brief One parsed file's payload + metadata.
 *
 * Returned by `IDirectoryLoaderSink::parseFile` and passed back to the
 * same sink's `commitBatch`. The `payload` is type-erased via `std::any`
 * so the loader library stays schema-agnostic — the sink produces it,
 * the sink consumes it, nobody in between peeks.
 */
struct ParsedEntry
{
    /// Registry key. Usually the JSON `"name"` field; sink-defined.
    QString key;

    /// Absolute path of the source file on disk.
    QString sourcePath;

    /// Set by the loader when this entry overrides an earlier-scanned
    /// one (user-wins-over-system on collision). Empty otherwise.
    ///
    /// INTROSPECTION METADATA ONLY — restore-on-delete is implemented
    /// by the rescan itself: when the user copy is removed, the next
    /// rescan re-parses the system file fresh and commits it as the
    /// authoritative entry, with `systemSourcePath` cleared. Sinks do
    /// not (and should not) consult this field at commit time to
    /// "restore" a system entry — there is nothing to restore, the
    /// system file has always been on disk and always gets re-parsed.
    ///
    /// Kept so UI code (settings browsers, layout managers) can show
    /// "your file shadows the system default at /path/to/system.json"
    /// to users without re-walking the system directory.
    QString systemSourcePath;

    /// Sink-owned payload (the parsed curve / profile / etc.). The
    /// sink that produced it is the sink that consumes it — safe
    /// to `std::any_cast` to the expected concrete type.
    std::any payload;
};

} // namespace PhosphorFsLoader
