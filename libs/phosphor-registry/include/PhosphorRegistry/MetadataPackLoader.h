// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/Registry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

QT_BEGIN_NAMESPACE
class QLoggingCategory;
QT_END_NAMESPACE

namespace PhosphorRegistry {

// Populates a Registry<Factory> from content packs discovered on disk —
// the metadata-pack counterpart to PluginLoader (which populates a
// Registry from .so + manifest bundles). Both feed the SAME generic
// Registry<T>; this is the seam that lets a domain registry (shaders,
// animation effects, …) compose Registry<T> for storage + notify while
// keeping a filesystem-scanned, hot-reloading content catalogue.
//
// ## What it does
//
//   - Owns a phosphor-fsloader MetadataPackScanStrategy<Entry> +
//     WatchedDirectorySet (the same scan/watch substrate the legacy
//     MetadataPackRegistryBase used), configured with a domain Parser
//     that turns one pack's metadata.json into a shared_ptr<Factory>.
//   - On every committed rescan, reconciles the Registry to match the
//     freshly-scanned set: NEW packs are registerFactory'd, REMOVED
//     packs unregisterFactory'd, and CHANGED packs (same id, different
//     content) re-registered. Reconciliation is per-entry by content
//     fingerprint, so an unchanged pack fires no spurious
//     register/unregister churn even when a sibling pack changed.
//
// ## Why fingerprint reconcile (not strategy-owned storage)
//
// MetadataPackScanStrategy rebuilds its pack map (and thus a fresh
// shared_ptr<Factory> per pack) on every rescan, and fires its OnCommit
// only when the cross-pack signature changed. A naive "drop all, re-add
// all" on commit would unregister + re-register every pack on any single
// pack's edit, so consumers connected to the Registry's per-entry
// notifier would see the whole catalogue churn. Diffing the committed
// set against a stored per-id fingerprint emits the minimal, accurate
// set of factoryRegistered / factoryUnregistered signals.
//
// ## Ownership / lifetime
//
//   - Does NOT own the Registry; the caller does, and the Registry MUST
//     outlive the loader. The loader holds a borrowed Registry pointer it
//     uses during LIVE rescans in reconcile() (where a removed pack calls
//     Registry::unregisterFactory); the defaulted destructor unregisters
//     nothing, so the registry simply outlives and is torn down after it.
//   - Owns the strategy + watcher. Declaration order is load-bearing:
//     m_strategy is declared before m_watcher so the watcher (which
//     holds a borrowed reference into the strategy) tears down first.
//
// ## Threading
//
//   GUI-thread only — inherits WatchedDirectorySet's contract. Every
//   mutating call (addSearchPaths / setUserPath / refresh) must run on
//   the construction thread.
//
//   Not re-entrant: reconcile() updates m_fingerprints AFTER driving the
//   Registry, whose change signals fire synchronously on this thread. A
//   slot that calls back into the loader (refresh / addSearchPaths) from
//   inside a Registry signal would run a nested reconcile against stale
//   fingerprints. Registry slots must not mutate the loader re-entrantly.
template<typename Factory>
class MetadataPackLoader
{
    static_assert(std::is_base_of_v<IFactoryBase, Factory>,
                  "MetadataPackLoader<T> requires T to derive from PhosphorRegistry::IFactoryBase");

public:
    // Turns one pack directory's parsed metadata.json into a factory, or
    // returns null to decline the pack (the strategy then skips it, with
    // a qDebug under the supplied logging category). @p isUser is true
    // when the pack was discovered under the user path (see setUserPath).
    using Parser =
        std::function<std::shared_ptr<Factory>(const QString& subdirPath, const QJsonObject& root, bool isUser)>;

    // Optional. Mixes a factory's content into the change-detection
    // hash, so a content edit to an existing pack (same id) is detected
    // and re-registered, while an unrelated pack's edit leaves this one
    // untouched. Without it, only the id set drives change detection
    // (add / remove only; in-place edits to a kept id are NOT seen).
    using SignatureContrib = std::function<void(QCryptographicHash&, const Factory&)>;

    // Optional. Extra files (beyond the pack's metadata.json, already
    // watched) whose changes should trigger a rescan — e.g. the shader
    // source files a pack references.
    using PerEntryWatchPaths = std::function<QStringList(const Factory&)>;

    // Optional. Files watched at the search-directory level rather than
    // per pack — e.g. shared GLSL includes (common.glsl) every pack pulls
    // in. An edit fires a rescan + the committed hook (so consumers
    // re-read content) but produces no per-entry registry change, since
    // it belongs to no single pack.
    using PerDirectoryWatchPaths = std::function<QStringList(const QString& searchPath)>;

    // Optional. Return true to skip a subdirectory during the scan — e.g.
    // a "shared" includes dir or a "none" sentinel that is not a pack.
    using PerSubdirSkip = std::function<bool(const QString& subdirName)>;

    // Optional. Fired once after every committed rescan (after the
    // registry reconcile), whether or not any per-entry change occurred.
    // The coarse "the catalogue was rescanned" hook a domain facade
    // bridges to its own contentsChanged / shadersChanged signal, so
    // consumers re-read content (re-bake a shader) even when the change
    // was a pack's source file or a shared include — neither of which
    // alters a pack's parsed metadata and so fires no per-entry signal.
    using CommittedCallback = std::function<void()>;

    // @p registry must be non-null and must outlive the loader — it is a
    // hard precondition (the loader is useless without a store and every
    // reconcile dereferences it). The debug Q_ASSERT_X catches a null in
    // development; passing null in a release build is a programmer-error
    // contract violation (undefined behaviour / crash on first reconcile),
    // not a recoverable condition. In practice callers pass the address of a
    // Registry member they own, so null cannot legitimately occur. @p parser
    // is required (a null parser would silently skip every pack). @p logCat is
    // stored by reference and must outlive the loader (a Q_LOGGING_CATEGORY
    // static is the standard source); it labels the scan/parse diagnostics.
    MetadataPackLoader(Registry<Factory>* registry, Parser parser, const QLoggingCategory& logCat)
        : m_registry(registry)
        , m_sigContrib() // set via setSignatureContrib before first scan
        , m_strategy(makeStrategy(std::move(parser)))
        , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, nullptr))
    {
        Q_ASSERT_X(m_registry != nullptr, "MetadataPackLoader", "registry must not be null");
        m_strategy->setLoggingCategory(logCat);
    }

    ~MetadataPackLoader() = default;
    Q_DISABLE_COPY_MOVE(MetadataPackLoader)

    // Mix factory content into the change hash. Call before the first
    // scan (i.e. before addSearchPaths) so the initial commit's
    // fingerprints are content-aware.
    void setSignatureContrib(SignatureContrib fn)
    {
        m_sigContrib = fn; // kept for reconcile()
        m_strategy->setSignatureContrib([fn = std::move(fn)](QCryptographicHash& hasher, const Entry& e) {
            if (fn && e.factory) {
                fn(hasher, *e.factory);
            }
        });
    }

    // Extra per-pack watch paths (the pack's own metadata.json is always
    // watched by the strategy).
    void setPerEntryWatchPaths(PerEntryWatchPaths fn)
    {
        m_strategy->setPerEntryWatchPaths([fn = std::move(fn)](const Entry& e) -> QStringList {
            return (fn && e.factory) ? fn(*e.factory) : QStringList{};
        });
    }

    // Watch shared / directory-level files (passthrough to the strategy).
    void setPerDirectoryWatchPaths(PerDirectoryWatchPaths fn)
    {
        m_strategy->setPerDirectoryWatchPaths(std::move(fn));
    }

    // Skip non-pack subdirectories during the scan (passthrough).
    void setPerSubdirSkip(PerSubdirSkip fn)
    {
        m_strategy->setPerSubdirSkip(std::move(fn));
    }

    // Set the coarse post-commit hook. Fires after reconcile on every
    // committed rescan. Set before the first scan so the initial commit
    // is observed.
    void setOnCommitted(CommittedCallback fn)
    {
        m_onCommitted = std::move(fn);
    }

    // Add search directories. Mirrors MetadataPackRegistryBase: a single
    // batched register runs one synchronous scan; the reconcile +
    // per-entry Registry signals fire inline before this returns.
    void
    addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On,
                   PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst)
    {
        const QStringList toRegister =
            PhosphorFsLoader::WatchedDirectorySet::filterNewSearchPaths(paths, m_watcher->directories());
        if (toRegister.isEmpty()) {
            return;
        }
        m_watcher->registerDirectories(toRegister, liveReload, order);
    }

    void addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On)
    {
        addSearchPaths(QStringList{path}, liveReload, PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
    }

    [[nodiscard]] QStringList searchPaths() const
    {
        return m_watcher->directories();
    }

    // Mark @p path as the user-data root for isUser classification.
    // Order-independent vs addSearchPaths; triggers a synchronous rescan
    // (reclassifying + reconciling) when the value changes and at least
    // one search path is already registered.
    void setUserPath(const QString& path)
    {
        if (m_userPath == path) {
            return;
        }
        m_userPath = path;
        m_strategy->setUserPath(path);
        if (!m_watcher->directories().isEmpty()) {
            m_watcher->rescanNow();
        }
    }

    // Synchronous rescan — re-walks every search path on the calling
    // stack and reconciles the Registry.
    void refresh()
    {
        m_watcher->rescanNow();
    }

private:
    // One scanned pack: the id the strategy keys on + the factory the
    // domain parser produced. `.id` (a QString field) satisfies
    // MetadataPackScanStrategy's Payload contract.
    struct Entry
    {
        QString id;
        std::shared_ptr<Factory> factory;
    };

    std::unique_ptr<PhosphorFsLoader::MetadataPackScanStrategy<Entry>> makeStrategy(Parser parser)
    {
        auto wrappedParser = [parser = std::move(parser)](const QString& subdir, const QJsonObject& root,
                                                          bool isUser) -> std::optional<Entry> {
            std::shared_ptr<Factory> factory = parser ? parser(subdir, root, isUser) : nullptr;
            if (!factory) {
                return std::nullopt;
            }
            return Entry{factory->id(), std::move(factory)};
        };
        return std::make_unique<PhosphorFsLoader::MetadataPackScanStrategy<Entry>>(std::move(wrappedParser), [this]() {
            reconcile();
            if (m_onCommitted) {
                m_onCommitted(); // coarse post-commit hook (after per-entry reconcile)
            }
        });
    }

    // Bring the Registry in line with the strategy's freshly-committed
    // pack set, emitting only the minimal per-entry signals.
    void reconcile()
    {
        QHash<QString, QByteArray> fresh;
        const QList<Entry> packs = m_strategy->packs();
        for (const Entry& e : packs) {
            QCryptographicHash hasher(QCryptographicHash::Sha1);
            hasher.addData(e.id.toUtf8());
            if (m_sigContrib && e.factory) {
                m_sigContrib(hasher, *e.factory);
            }
            const QByteArray fp = hasher.result();
            fresh.insert(e.id, fp);

            const auto prev = m_fingerprints.constFind(e.id);
            if (prev == m_fingerprints.cend()) {
                m_registry->registerFactory(e.factory); // newly discovered
            } else if (prev.value() != fp) {
                // Content changed: replace in place. Replace (not a separate
                // unregister + register) keeps the pack's REGISTRATION-ORDER
                // position — a hot-reload must not shuffle the edited pack to
                // the end of ids()/forEach() (Registry's documented invariant).
                // It still fires factoryUnregistered(old) + factoryRegistered(new).
                // (m_strategy->packs() is id-sorted, so the INITIAL scan registers
                // in lexicographic id order; a pack discovered on a later refresh
                // appends, so consumers that need a sorted order re-sort ids()
                // themselves rather than relying on loader registration order.)
                m_registry->registerFactory(e.factory, QString(), DuplicatePolicy::Replace);
            }
            // else: unchanged — leave the registry entry as-is.
        }
        // Remove packs that disappeared. Walk the registry's ids() — which is
        // registration order — rather than m_fingerprints (a QHash, hash order)
        // so the per-removal factoryUnregistered signals stay in registration
        // order, matching the contract Registry::unregisterByOwner upholds and
        // consumers rely on. The m_fingerprints.contains() filter restricts the
        // removal to packs THIS loader registered (it is the sole writer of its
        // borrowed Registry), never an entry registered out-of-band.
        const QList<QString> registered = m_registry->ids();
        for (const QString& id : registered) {
            if (m_fingerprints.contains(id) && !fresh.contains(id)) {
                m_registry->unregisterFactory(id); // pack disappeared
            }
        }
        m_fingerprints = std::move(fresh);
    }

    // Non-owning; caller guarantees lifetime. Not a QPointer: Registry<T>
    // is not a QObject (its notifier is).
    Registry<Factory>* m_registry = nullptr;
    SignatureContrib m_sigContrib;
    CommittedCallback m_onCommitted;
    QString m_userPath;
    // Last committed per-id content fingerprint, for minimal reconcile.
    QHash<QString, QByteArray> m_fingerprints;
    // Declaration order load-bearing: m_strategy before m_watcher (the
    // watcher borrows a reference into the strategy).
    std::unique_ptr<PhosphorFsLoader::MetadataPackScanStrategy<Entry>> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
};

} // namespace PhosphorRegistry
