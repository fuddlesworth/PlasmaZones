// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorJsonLoader/ParsedEntry.h>

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>

namespace PhosphorJsonLoader {

/**
 * @brief Consumer-supplied strategy for a `DirectoryLoader`.
 *
 * The loader handles directory walking, file watching, debouncing, and
 * user-wins-collision bookkeeping; the sink handles per-schema concerns:
 *
 *   • `parseFile()` — turn one file into a `ParsedEntry` (or skip it).
 *     Pure parse; MUST NOT register anything with the target registry.
 *   • `commitBatch()` — apply the full rescan batch (removed + current).
 *     This is the one place the sink touches its registry, so bulk
 *     signals (e.g. `reloadAll`) coalesce naturally to one emit per scan.
 *
 * Implementations are typically per-schema (e.g. `CurveLoaderSink`,
 * `ProfileLoaderSink`) and stateless aside from a registry back-reference.
 */
class IDirectoryLoaderSink
{
public:
    virtual ~IDirectoryLoaderSink() = default;

    /**
     * @brief Parse one file. Pure — no registry side effects.
     *
     * Return an engaged optional with the parsed key + payload on
     * success; return `std::nullopt` to silently skip the file (the
     * sink is responsible for logging the reason at its own category).
     *
     * `ParsedEntry::systemSourcePath` is ignored on return — the
     * loader fills it if this entry overrides an earlier-scanned one.
     */
    virtual std::optional<ParsedEntry> parseFile(const QString& filePath) = 0;

    /**
     * @brief Apply the result of a full rescan to the sink's registry.
     *
     * @param removedKeys  Keys that were previously in the registry
     *                     but are no longer present on disk. The sink
     *                     must unregister each from its target registry.
     * @param currentEntries  Every entry currently on disk (after
     *                     user-wins collision resolution). The sink
     *                     registers / replaces each. Entries whose
     *                     payload is byte-identical to the already-
     *                     registered value should produce no signal —
     *                     the target registry's equality guard handles
     *                     this; the sink just calls `registerXxx`.
     *
     * Called exactly once per rescan, after the full directory walk
     * is complete. Sinks are free to batch-replace via a bulk
     * registry call (e.g. `PhosphorProfileRegistry::reloadAll`) or
     * iterate the list — the loader imposes no shape.
     */
    virtual void commitBatch(const QStringList& removedKeys, const QList<ParsedEntry>& currentEntries) = 0;
};

} // namespace PhosphorJsonLoader
