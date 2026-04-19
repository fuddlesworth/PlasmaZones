// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QString>

#include <any>

namespace PhosphorJsonLoader {

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
    /// The shadowed entry's `sourcePath` is preserved here so the
    /// consumer can restore it when the user copy is deleted.
    QString systemSourcePath;

    /// Sink-owned payload (the parsed curve / profile / etc.). The
    /// sink that produced it is the sink that consumes it — safe
    /// to `std::any_cast` to the expected concrete type.
    std::any payload;
};

} // namespace PhosphorJsonLoader
