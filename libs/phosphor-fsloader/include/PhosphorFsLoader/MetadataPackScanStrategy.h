// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IScanStrategy.h>

#include <QtCore/QByteArray>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <algorithm>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace PhosphorFsLoader {

/**
 * @brief Reusable scan strategy for `metadata.json`-driven subdirectory pack registries.
 *
 * Owns the cross-cutting scaffolding both `PhosphorShaders::ShaderPackRegistry`
 * and `PhosphorAnimationShaders::AnimationShaderRegistry` were duplicating
 * verbatim:
 *
 *   1. Reverse-iterate `directoriesInScanOrder` (the canonical
 *      `[lowest-priority, ..., highest-priority]` shape from
 *      `WatchedDirectorySet`); first-registration-wins on `id`
 *      collision yields the XDG semantic
 *      `user > sys-highest > ... > sys-lowest`.
 *   2. Walk every subdirectory under each registered search path,
 *      build the `metadata.json` path, and stat-cap the file at
 *      `DirectoryLoader::kMaxFileBytes` before reading (untrusted
 *      same-user blob DoS guard).
 *   3. Parse `metadata.json` into a caller-supplied schema-specific
 *      `Payload` via the `Parser` policy. Parser-returned `std::nullopt`
 *      skips the entry; an empty `Payload::id` skips it as well.
 *   4. Per-rescan entry cap (caller-tunable, default 10,000) — when
 *      tripped, system overflow is dropped (reverse-iteration scans
 *      user dirs first, so cap-trip never violates user-wins layering).
 *   5. SHA-1 signature over the sorted-by-id entry set + caller-
 *      supplied per-payload contribution; `OnCommit` is invoked only
 *      when the signature differs from the previous scan, giving the
 *      consumer change-only emit semantics on top of `WatchedDirectorySet`'s
 *      unconditional `rescanCompleted`.
 *   6. Sorted-by-id accessor (`packs()`) so QHash randomisation never
 *      surfaces in downstream UI / snapshot tests.
 *   7. `isUser` classification per entry, derived from a caller-supplied
 *      user-path and per-iterated-dir canonicalisation.
 *
 * Schema-specific concerns are delegated to caller-supplied policy
 * callables held by reference internally:
 *
 *   - `Parser` — turns one `(subdirPath, jsonRoot, isUser)` into an
 *     optional `Payload`. The strategy DOES NOT prescribe which JSON
 *     fields the parser consumes; mutating the payload's `id`,
 *     applying directory-relative path resolution, and any inline
 *     validation (e.g. multipass-needs-buffer-shader) all happen here.
 *
 *   - `PerEntryWatchPaths` — extracts the per-payload watch paths
 *     (frag/vert/kwin/include shaders, additional metadata) the
 *     `WatchedDirectorySet` base must re-arm individual file watches
 *     on after every rescan. The strategy itself always watches the
 *     `metadata.json`; the callback adds payload-internal paths.
 *
 *   - `PerDirectoryWatchPaths` (optional) — additional paths to watch
 *     per registered search path, NOT per pack. Used by the shader-pack
 *     registry to watch top-level shared `*.glsl` includes.
 *
 *   - `PerSubdirSkip` (optional) — predicate over the bare subdirectory
 *     name. Used by the shader-pack registry to reserve `none` as a
 *     "no shader" sentinel.
 *
 *   - `SignatureContrib` (optional) — fans the payload-specific
 *     fingerprint bytes into the running SHA-1. The strategy itself
 *     contributes `id` + `isUser` + the metadata.json's size+mtime;
 *     callers add whatever else change-detection needs (frag-shader
 *     mtime+size, source-dir absolute path, etc.).
 *
 *   - `OnCommit` — invoked synchronously inside `performScan` after the
 *     fresh map has replaced the prior one, AND ONLY WHEN the SHA-1
 *     signature differs. The consumer wires its content-changed signal
 *     in here. NOT invoked when the scan is empty AND the previous
 *     scan was also empty (the no-content baseline doesn't fire on
 *     repeated empty scans).
 *
 * ## API choice — why a class template over a virtual interface
 *
 * Both real consumers (`ShaderInfo` in `phosphor-shaders`, `AnimationShaderEffect`
 * in `phosphor-animation-shaders`) parse into concrete struct types they
 * already own. A virtual `IMetadataPackPayload` interface would force
 * each registry to box its parse output, hide its public payload type
 * behind a base, and add a vtable hop on every signature/watch-extract
 * call. A class template lets each registry keep its concrete payload
 * type, inline the policy calls, and share the per-rescan map via
 * direct accessors. The template is also header-only, which keeps
 * `phosphor-fsloader` itself free of dependencies on either consumer's
 * payload schema.
 *
 * The price is one set of compiler instantiations per consumer payload —
 * negligible given there are exactly two of them.
 *
 * ## Lifetime
 *
 * The strategy is constructed by the consumer registry, held by
 * reference inside its `WatchedDirectorySet`, and destroyed when the
 * registry is. `Parser` / watch-extractor callables are stored by value
 * (typically `std::function`); they may capture state by pointer/reference
 * if the captured state outlives the registry.
 *
 * ## Thread safety
 *
 * GUI-thread only. Inherits `WatchedDirectorySet`'s threading constraint —
 * `performScan` runs on the same thread the registry was constructed on.
 *
 * @tparam Payload  POD-ish struct exposing a public `QString id` member.
 *                  Must be default-constructible and movable. Both real
 *                  consumers (`ShaderInfo`, `AnimationShaderEffect`) satisfy
 *                  this. Bespoke payloads without an `id` field can wrap
 *                  their data in a thin POD that adds one.
 */
template<typename Payload>
class MetadataPackScanStrategy : public IScanStrategy
{
    // The strategy hashes `id` into the per-rescan signature and uses it
    // as the QHash key for first-wins layering — neither works without
    // a public QString id member. ShaderInfo and AnimationShaderEffect
    // both satisfy this; bespoke payloads must too.
    //
    // `decltype(... .id)` on an lvalue Payload yields `QString&`; strip
    // the reference before comparing so the assertion fires only when
    // the field's type itself isn't `QString`.
    static_assert(std::is_same_v<std::remove_reference_t<decltype(std::declval<Payload&>().id)>, QString>,
                  "MetadataPackScanStrategy<Payload> requires Payload to expose a public 'QString id' member.");

public:
    /// Hard cap on entries discovered per rescan, summed across every
    /// registered search path. Mirrors `DirectoryLoader::kMaxEntries`
    /// and the per-consumer caps the two registries hard-coded
    /// (`kMaxShaders`, `kMaxEffects` = 10'000 each). Typical pack
    /// counts are single digits — the cap is purely a DoS guard
    /// against pathological user dirs with thousands of
    /// metadata.json-bearing subdirs.
    static constexpr int kDefaultMaxEntries = 10'000;

    /// Parse one `metadata.json` into a payload. The strategy already
    /// validated (a) the file exists, (b) it is under
    /// `DirectoryLoader::kMaxFileBytes`, (c) parsing succeeded and the
    /// root is a JSON object, before invoking the parser. Returning
    /// `std::nullopt` skips the entry silently; the strategy will warn
    /// at the call site below where appropriate.
    ///
    /// The parser is responsible for resolving directory-relative paths
    /// inside @p root against @p subdirPath, applying any inline
    /// validation, and stamping the payload's `isUser` flag from
    /// @p isUser if the schema exposes one. Returned payloads with an
    /// empty `id` are dropped by the caller (warning logged).
    using Parser =
        std::function<std::optional<Payload>(const QString& subdirPath, const QJsonObject& root, bool isUser)>;

    /// Extract the per-payload paths the base must re-arm individual
    /// file watches on after every rescan. The strategy always adds
    /// the metadata.json itself; this callback is for everything else
    /// (frag / vert / kwin shaders, additional metadata, etc.).
    using PerEntryWatchPaths = std::function<QStringList(const Payload&)>;

    /// Optional: per-search-path watch additions beyond per-pack
    /// extraction. Use for shared top-level files inside the search
    /// path (e.g. `common.glsl` in the shader-pack registry).
    /// Default: returns an empty list.
    using PerDirectoryWatchPaths = std::function<QStringList(const QString& searchPath)>;

    /// Optional: predicate skipping subdirectories whose bare name
    /// matches a sentinel. Default: never skip. The shader-pack
    /// registry uses this to reserve `none` for "no shader".
    using PerSubdirSkip = std::function<bool(const QString& subdirName)>;

    /// Optional: payload-specific bytes to fan into the per-rescan
    /// SHA-1 signature. The strategy already mixes in `id`, `isUser`,
    /// and the metadata.json's size+mtime; callers add what change-
    /// detection further requires (e.g. frag-shader mtime+size for
    /// edit-on-disk wake). Default: contributes nothing extra.
    using SignatureContrib = std::function<void(QCryptographicHash&, const Payload&)>;

    /// Synchronous "the discovered set changed" hook. Invoked from
    /// inside `performScan` after the fresh map replaces the previous
    /// one. The consumer wires its public content-changed signal
    /// emission in here. The strategy guarantees the signature has
    /// actually changed before invoking; consumers don't double-diff.
    using OnCommit = std::function<void()>;

    /**
     * @brief Construct with the parser + commit hook (the two
     *        always-required policies). Optional policies default to
     *        no-ops via the setter API below.
     *
     * @param parser     Schema-specific `metadata.json` → `Payload` parser.
     * @param onCommit   Called only when the per-rescan signature
     *                   differs from the previous scan (empty parser
     *                   results compared to a previous empty scan are
     *                   not commits).
     */
    MetadataPackScanStrategy(Parser parser, OnCommit onCommit)
        : m_parser(std::move(parser))
        , m_onCommit(std::move(onCommit))
    {
    }

    ~MetadataPackScanStrategy() override = default;

    MetadataPackScanStrategy(const MetadataPackScanStrategy&) = delete;
    MetadataPackScanStrategy& operator=(const MetadataPackScanStrategy&) = delete;

    /// Set the per-payload watch-extractor. Default: empty list.
    void setPerEntryWatchPaths(PerEntryWatchPaths fn)
    {
        m_perEntryWatch = std::move(fn);
    }

    /// Set the per-directory watch-extractor. Default: empty list.
    void setPerDirectoryWatchPaths(PerDirectoryWatchPaths fn)
    {
        m_perDirWatch = std::move(fn);
    }

    /// Set the per-subdir-name skip predicate. Default: never skip.
    void setPerSubdirSkip(PerSubdirSkip fn)
    {
        m_subdirSkip = std::move(fn);
    }

    /// Set the payload-specific signature contributor. Default: contributes nothing.
    void setSignatureContrib(SignatureContrib fn)
    {
        m_sigContrib = std::move(fn);
    }

    /**
     * @brief Set the user-data search path used for `isUser` classification.
     *
     * The strategy canonicalises this via `QFileInfo::canonicalFilePath`
     * once per rescan and compares each iterated dir's canonical form
     * against it. Empty (the default) yields `false` for every entry's
     * `isUser`. The path does not need to exist at the time of the
     * call — `canonicalFilePath` resolves once it materialises and
     * subsequent rescans pick up the classification.
     */
    void setUserPath(const QString& path)
    {
        m_userPath = path;
    }

    /// Per-rescan entry cap. Default: `kDefaultMaxEntries`.
    void setMaxEntries(int cap)
    {
        m_maxEntries = cap;
    }

    /// Optional: the logging category to emit warnings under (cap
    /// trip, oversized metadata.json, parse error, missing id). When
    /// nullptr, the strategy uses its own internal category. Both
    /// real consumers pass their own so log output keeps its
    /// per-registry filterability.
    void setLoggingCategory(const QLoggingCategory* cat)
    {
        m_loggingCat = cat;
    }

    /**
     * @brief Run a full rescan across @p directoriesInScanOrder.
     *
     * See `IScanStrategy::performScan` for the canonical input shape.
     * The strategy always rebuilds its full pack map from scratch — no
     * incremental update. Stale entries (subdirs whose `metadata.json`
     * vanished since the last scan) drop out by being absent from the
     * rebuilt map; the next signature comparison reports the change
     * and `OnCommit` fires.
     *
     * @return Per-rescan watch paths: every iterated subdir's
     *         `metadata.json`, plus everything `PerEntryWatchPaths`
     *         and `PerDirectoryWatchPaths` returned. Does NOT include
     *         the registered search paths themselves — the base
     *         already watches those directly.
     */
    QStringList performScan(const QStringList& directoriesInScanOrder) override;

    // ─── Accessors used by the consumer registry ────────────────────────────

    /// Live entries by id.
    const QHash<QString, Payload>& packsById() const
    {
        return m_packs;
    }

    /// Live entries sorted by id for deterministic ordering. QHash
    /// iteration order is randomised in Qt6; consumers exposing pack
    /// lists to UI / tests should use this rather than `packsById()`.
    QList<Payload> packs() const
    {
        QList<Payload> sorted = m_packs.values();
        std::sort(sorted.begin(), sorted.end(), [](const Payload& a, const Payload& b) {
            return a.id < b.id;
        });
        return sorted;
    }

    /// Lookup by id. Returns a default-constructed `Payload` if the id
    /// is not registered (consistent with `QHash::value`).
    Payload pack(const QString& id) const
    {
        return m_packs.value(id);
    }

    /// True if @p id is registered.
    bool contains(const QString& id) const
    {
        return m_packs.contains(id);
    }

    /// Number of currently registered packs.
    int size() const
    {
        return m_packs.size();
    }

private:
    Parser m_parser;
    OnCommit m_onCommit;
    PerEntryWatchPaths m_perEntryWatch;
    PerDirectoryWatchPaths m_perDirWatch;
    PerSubdirSkip m_subdirSkip;
    SignatureContrib m_sigContrib;

    QString m_userPath;
    int m_maxEntries = kDefaultMaxEntries;
    const QLoggingCategory* m_loggingCat = nullptr;

    QHash<QString, Payload> m_packs;
    QByteArray m_lastSignature;
    bool m_signatureSeeded = false;
};

// ─── Template implementation ─────────────────────────────────────────────────

namespace detail {

/// Internal logging category for the strategy's own warnings when the
/// caller didn't supply one via `setLoggingCategory`.
PHOSPHORFSLOADER_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcMetadataPackScan)

} // namespace detail

template<typename Payload>
QStringList MetadataPackScanStrategy<Payload>::performScan(const QStringList& directoriesInScanOrder)
{
    QHash<QString, Payload> fresh;
    QStringList desiredWatches;

    const QLoggingCategory& log = m_loggingCat ? *m_loggingCat : detail::lcMetadataPackScan();

    // Resolve the user path's canonical form once per rescan. Empty
    // (no user path configured, or the path doesn't exist yet) yields
    // `false` for every dir — the iterated-dir compare below short-
    // circuits when this is empty.
    const QString canonicalUserPath = m_userPath.isEmpty() ? QString() : QFileInfo(m_userPath).canonicalFilePath();

    bool capTripped = false;

    // Reverse-iterate: highest-priority dirs first, first-wins on id
    // collision. The base normalises caller input into the canonical
    // `[lowest, ..., highest]` shape at registration time, so this
    // reversal is the SSOT for the user-wins semantic the two
    // consumer registries promise.
    for (auto dirIt = directoriesInScanOrder.crbegin(); dirIt != directoriesInScanOrder.crend() && !capTripped;
         ++dirIt) {
        const QString& searchPath = *dirIt;
        QDir dirObj(searchPath);
        if (!dirObj.exists()) {
            qCDebug(log) << "MetadataPackScanStrategy: search path does not exist:" << searchPath;
            continue;
        }

        const bool isUserDir =
            !canonicalUserPath.isEmpty() && QFileInfo(searchPath).canonicalFilePath() == canonicalUserPath;

        // Per-search-path watch additions (top-level shared files —
        // shader-pack registry watches `*.glsl` includes here).
        if (m_perDirWatch) {
            desiredWatches.append(m_perDirWatch(searchPath));
        }

        const QStringList subdirs = dirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& subdir : subdirs) {
            if (m_subdirSkip && m_subdirSkip(subdir)) {
                continue;
            }
            // Per-rescan entry-count DoS guard. Reverse-iteration scans
            // user-first / system-last, so cap-trip drops *system*
            // overflow rather than user overrides.
            if (fresh.size() >= m_maxEntries) {
                capTripped = true;
                break;
            }

            const QString subdirPath = dirObj.filePath(subdir);
            const QString metadataPath = subdirPath + QStringLiteral("/metadata.json");

            // Always re-arm the metadata.json watch — even if parsing
            // fails or the id collides. An edit that fixes a broken
            // metadata.json is the most common way an entry transitions
            // from invisible to visible; we want to wake on it.
            desiredWatches.append(metadataPath);

            const QFileInfo metadataInfo(metadataPath);
            if (!metadataInfo.exists()) {
                qCDebug(log) << "MetadataPackScanStrategy: skipping subdir, no metadata.json:" << subdirPath;
                continue;
            }

            // DoS guard: untrusted same-user metadata.json must not
            // stall the GUI thread with a 2 GB blob. Reuse
            // `DirectoryLoader::kMaxFileBytes` as the SSOT — same cap
            // the sister `JsonScanStrategy` enforces on every JSON
            // file it loads.
            if (metadataInfo.size() > DirectoryLoader::kMaxFileBytes) {
                qCWarning(log) << "MetadataPackScanStrategy: skipping oversized metadata.json:" << metadataPath << "("
                               << metadataInfo.size() << "bytes, cap" << DirectoryLoader::kMaxFileBytes << ")";
                continue;
            }

            QFile file(metadataPath);
            if (!file.open(QIODevice::ReadOnly)) {
                qCWarning(log) << "MetadataPackScanStrategy: failed to open metadata.json:" << metadataPath;
                continue;
            }

            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                qCWarning(log) << "MetadataPackScanStrategy: parse error in" << metadataPath << ":"
                               << parseError.errorString();
                continue;
            }
            if (!doc.isObject()) {
                qCWarning(log) << "MetadataPackScanStrategy: non-object root in" << metadataPath;
                continue;
            }

            // Schema-specific parse.
            std::optional<Payload> parsed = m_parser ? m_parser(subdirPath, doc.object(), isUserDir) : std::nullopt;
            if (!parsed.has_value()) {
                qCDebug(log) << "MetadataPackScanStrategy: parser declined" << metadataPath;
                continue;
            }
            if (parsed->id.isEmpty()) {
                qCWarning(log) << "MetadataPackScanStrategy: skipping" << metadataPath << ": empty 'id' field";
                continue;
            }

            // First-wins on id collision. Reverse-iteration means a
            // user-dir entry claims its id before any system-dir entry
            // can; a colliding system entry is silently shadowed.
            if (fresh.contains(parsed->id)) {
                qCDebug(log) << "MetadataPackScanStrategy: id" << parsed->id
                             << "already registered from a higher-priority dir; shadowed at:" << subdirPath;
                continue;
            }

            // Per-payload watches — frag/vert/kwin shaders, etc.
            if (m_perEntryWatch) {
                desiredWatches.append(m_perEntryWatch(*parsed));
            }

            fresh.insert(parsed->id, std::move(*parsed));
        }
    }

    if (capTripped) {
        qCWarning(log).nospace() << "MetadataPackScanStrategy: reached entry cap (" << m_maxEntries
                                 << ") — later entries skipped to protect the GUI thread. Prune the watched search "
                                    "paths or raise the cap.";
    }

    // SHA-1 signature over (id, isUser, metadata.json size+mtime,
    // payload-specific bytes). Stable iteration order is enforced via
    // sorted ids; QHash randomisation never leaks into the signature.
    QCryptographicHash hasher(QCryptographicHash::Sha1);
    QList<QString> sortedIds = fresh.keys();
    std::sort(sortedIds.begin(), sortedIds.end());
    for (const QString& id : std::as_const(sortedIds)) {
        const Payload& p = fresh.value(id);
        hasher.addData(id.toUtf8());
        hasher.addData(QByteArrayView("|"));
        if (m_sigContrib) {
            m_sigContrib(hasher, p);
        }
        hasher.addData(QByteArrayView("\n"));
    }
    const QByteArray signature = hasher.result();

    const bool isFirstScan = !m_signatureSeeded;
    const bool changed = isFirstScan ? !fresh.isEmpty() : signature != m_lastSignature;

    m_packs = std::move(fresh);
    m_lastSignature = signature;
    m_signatureSeeded = true;

    if (changed && m_onCommit) {
        m_onCommit();
    }

    return desiredWatches;
}

} // namespace PhosphorFsLoader
