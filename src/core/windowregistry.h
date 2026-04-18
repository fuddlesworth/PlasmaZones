// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QHash>
#include <QMultiHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace PlasmaZones {

/**
 * @brief Mutable per-window metadata owned by WindowRegistry.
 *
 * Everything in this struct is mutable over a window's lifetime and must be
 * looked up via WindowRegistry — never parsed out of an instance id string.
 *
 * Instance id is intentionally absent: it's the primary key used to retrieve
 * a record, not part of the record itself.
 */
struct WindowMetadata
{
    QString appId; ///< current app class (mutable — Electron/CEF apps swap this after mapping)
    QString desktopFile; ///< current desktop file name (mutable)
    QString title; ///< current caption (mutable)

    bool operator==(const WindowMetadata& other) const
    {
        return appId == other.appId && desktopFile == other.desktopFile && title == other.title;
    }
    bool operator!=(const WindowMetadata& other) const
    {
        return !(*this == other);
    }
};

/**
 * @brief Single source of truth for live-window instance identity and metadata.
 *
 * Design goals:
 * - Instance id is the ONLY primary key. Compositor-supplied, opaque, stable
 *   for a window's lifetime. Core code must not parse it.
 * - Metadata (appId, desktopFile, title) is mutable. KWin can swap WM_CLASS /
 *   desktopFileName at runtime for apps like Emby (CEF/Electron). Consumers
 *   subscribe to metadataChanged to react — but per the product decision
 *   recorded in feedback_class_change_exclusion.md, reactions are limited to
 *   updating internal tracking, not retroactively enforcing rules on committed
 *   state.
 * - Portable: the compositor bridge (kwin-effect today) produces instance ids
 *   and metadata by whatever means its compositor exposes. A future Hyprland
 *   bridge populates the same registry with hyprctl addresses and class info.
 *   The core daemon never knows which compositor produced a given id.
 *
 * Lifecycle:
 *   bridge observes new window    → upsert(id, metadata)    → windowAppeared
 *   bridge observes class change  → upsert(id, newMetadata) → metadataChanged
 *   bridge observes window closed → remove(id)              → windowDisappeared
 *
 * This class is purely in-memory. It's owned by the daemon root and outlives
 * all consumers.
 */
class PLASMAZONES_EXPORT WindowRegistry : public QObject
{
    Q_OBJECT

public:
    explicit WindowRegistry(QObject* parent = nullptr);
    ~WindowRegistry() override;

    // ──────────────────────────────────────────────────────────────────────
    // Bridge-facing API (called by compositor integration layer)
    // ──────────────────────────────────────────────────────────────────────

    /**
     * @brief Insert a new window or update metadata for an existing one.
     *
     * If @p instanceId is unknown, emits windowAppeared.
     * If it's already known and metadata differs, emits metadataChanged.
     * No-op (no signal) if metadata is unchanged — so bridges can call this
     * unconditionally on every observation without creating spurious churn.
     *
     * Empty instanceId is rejected (logged warning, no state change).
     * Whitespace-only appId is accepted and stored as-is; consumers decide
     * whether to skip rule evaluation for such transient classes.
     */
    void upsert(const QString& instanceId, const WindowMetadata& metadata);

    /**
     * @brief Remove a window record (window closed).
     *
     * Emits windowDisappeared if the id was known.
     * No-op if unknown.
     */
    void remove(const QString& instanceId);

    // ──────────────────────────────────────────────────────────────────────
    // Consumer-facing API (daemon services query these)
    // ──────────────────────────────────────────────────────────────────────

    /**
     * @brief Look up the full metadata record for an instance.
     * @return nullopt if the instance is not known.
     */
    std::optional<WindowMetadata> metadata(const QString& instanceId) const;

    /**
     * @brief Convenience: current appId for an instance, or empty if unknown.
     *
     * Use this in place of every PhosphorIdentity::WindowId::extractAppId(windowId) call site.
     * Unknown windows return QString() — callers must handle that case.
     */
    QString appIdFor(const QString& instanceId) const;

    /**
     * @brief All live instance ids whose current appId matches @p appId.
     *
     * Returns exact-match instances; consumers that need segment-aware matching
     * (appIdMatches) should iterate allInstances() and filter.
     */
    QStringList instancesWithAppId(const QString& appId) const;

    /**
     * @brief True if the registry has a record for this instance.
     */
    bool contains(const QString& instanceId) const;

    /**
     * @brief All currently-known instance ids.
     */
    QStringList allInstances() const;

    /**
     * @brief Count of tracked windows. Useful for tests and diagnostics.
     */
    int size() const;

    /**
     * @brief Clear all state. Called on daemon shutdown or full reset.
     */
    void clear();

    // ──────────────────────────────────────────────────────────────────────
    // Canonicalization
    // ──────────────────────────────────────────────────────────────────────

    /**
     * @brief Translate any windowId-shaped string to the canonical key used
     *        by services that key internal state by windowId.
     *
     * Given either a bare instance id or a legacy "appId|uuid" composite,
     * this returns a stable string that every call site in the daemon can
     * agree on for a given instance.
     *
     * Semantics: the canonical form is the FIRST windowId string ever seen
     * for a given instance id. Subsequent arrivals with a mutated appId
     * resolve back to the original string, so service maps keyed by windowId
     * stay consistent even when KWin rebroadcasts the window under a new
     * class.
     *
     * This is stateful — the first call for an unknown instance id locks
     * that instance's canonical form. releaseCanonical() is how a window's
     * closure clears the entry; otherwise, the canonical map grows with
     * every live window and prunes only on daemon shutdown.
     *
     * Thread safety: call only from the main (Qt event loop) thread.
     */
    QString canonicalizeWindowId(const QString& rawWindowId);

    /**
     * @brief Const-safe variant for read-only call sites.
     *
     * Returns the canonical form if the instance id is already known, or the
     * raw input otherwise. Never mutates state. Use in const methods that
     * want a best-effort lookup without committing to a canonical form.
     */
    QString canonicalizeForLookup(const QString& rawWindowId) const;

    /**
     * @brief Release a window's canonical entry.
     *
     * Called from the compositor bridge's close path after all consumer
     * cleanup has run. Safe to call with an id that's already canonical or
     * never seen.
     */
    void releaseCanonical(const QString& anyWindowId);

Q_SIGNALS:
    /**
     * @brief A previously unknown instance was registered.
     */
    void windowAppeared(const QString& instanceId);

    /**
     * @brief A known instance's metadata changed.
     *
     * Subscribers per feedback_class_change_exclusion.md: update internal
     * tracking (so future lookups see the new class), but do NOT retroactively
     * re-evaluate rules, exclusions, or auto-snap for committed state. The one
     * legitimate action is consuming pending session-restore entries that were
     * waiting for a real class.
     */
    void metadataChanged(const QString& instanceId, const WindowMetadata& oldMetadata,
                         const WindowMetadata& newMetadata);

    /**
     * @brief An instance was removed (window closed).
     */
    void windowDisappeared(const QString& instanceId);

private:
    QHash<QString, WindowMetadata> m_records;
    // Reverse index for O(1) appId → instances lookup. Kept in sync with m_records.
    QMultiHash<QString, QString> m_appIdIndex;
    // Instance id → first-seen canonical windowId shared across all services.
    QHash<QString, QString> m_canonicalByInstance;

    // Helpers
    void indexInsert(const QString& instanceId, const QString& appId);
    void indexRemove(const QString& instanceId, const QString& appId);
};

} // namespace PlasmaZones
