// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <PhosphorConfig/IBackend.h>

#include <QJsonObject>
#include <QString>

#include <memory>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace PhosphorConfig {

class IGroupPathResolver;
class JsonBackend;

/// Scoped group view backed by a @c JsonBackend. See @c IGroup for the
/// contract; this class only adds JSON-specific coercion rules documented
/// on its reader methods.
///
/// Only one group view is permitted per backend at a time. Debug builds
/// assert; release builds warn. The backend advances its dirty flag on any
/// write-to-storage through this view.
class PHOSPHORCONFIG_EXPORT JsonGroup : public IGroup
{
public:
    JsonGroup(QJsonObject& root, QString groupName, JsonBackend* backend);
    ~JsonGroup() override;

    QString readString(const QString& key, const QString& defaultValue = {}) const override;
    int readInt(const QString& key, int defaultValue = 0) const override;
    bool readBool(const QString& key, bool defaultValue = false) const override;
    double readDouble(const QString& key, double defaultValue = 0.0) const override;
    QColor readColor(const QString& key, const QColor& defaultValue = {}) const override;

    void writeString(const QString& key, const QString& value) override;
    void writeInt(const QString& key, int value) override;
    void writeBool(const QString& key, bool value) override;
    void writeDouble(const QString& key, double value) override;
    void writeColor(const QString& key, const QColor& value) override;
    void writeJson(const QString& key, const QJsonValue& value) override;
    QJsonValue readJson(const QString& key, const QJsonValue& defaultValue = {}) const override;

    bool hasKey(const QString& key) const override;
    void deleteKey(const QString& key) override;
    QStringList keyList() const override;

private:
    QJsonObject groupObject() const;
    void setGroupObject(const QJsonObject& obj);
    /// Returns true when writes through this group must be silently dropped
    /// because another JsonGroup on the same backend was already alive at
    /// construction time. See JsonGroup constructor docs.
    bool refuseWrite(const char* op) const;

    QJsonObject& m_root;
    QString m_groupName;
    JsonBackend* m_backend;
    /// Set in the constructor when the single-active-group invariant is
    /// violated. Disabled groups still allow reads (they share the backend's
    /// root in-memory state) but reject writes so the concurrent live group
    /// retains sole ownership of mutations.
    bool m_disabled = false;
};

/// Atomic-write JSON configuration backend.
///
/// Reads and writes a single JSON document. Writes are deferred until
/// @c sync() is called and then committed via @c QSaveFile (temp file +
/// rename, safe against crashes and power loss).
///
/// Group resolution order:
///   1. If an @c IGroupPathResolver is installed and returns a path, use it.
///   2. If the group name contains a dot, treat as a dot-path hierarchy.
///   3. Otherwise treat it as a flat top-level object name.
///
/// The "root group" (see @c rootGroupName) holds ungrouped keys written via
/// @c writeRootString. Defaults to "General" for compatibility with
/// QSettings INI behaviour.
class PHOSPHORCONFIG_EXPORT JsonBackend : public IBackend
{
public:
    /// Construct a backend backed by @p filePath. The file is loaded
    /// immediately; missing or malformed files start with an empty root
    /// object (a warning is logged for malformed content).
    explicit JsonBackend(QString filePath);
    ~JsonBackend() override;

    // IBackend interface
    std::unique_ptr<IGroup> group(const QString& name) override;
    void reparseConfiguration() override;
    bool sync() override;
    void deleteGroup(const QString& name) override;
    QString readRootString(const QString& key, const QString& defaultValue = {}) const override;
    void writeRootString(const QString& key, const QString& value) override;
    void removeRootKey(const QString& key) override;
    QStringList groupList() const override;

    /// Attach a custom path resolver. Passing @c nullptr removes it.
    /// Safe to call at any time; the resolver is consulted on every group
    /// access, so it should be cheap.
    void setPathResolver(std::shared_ptr<IGroupPathResolver> resolver) override;
    std::shared_ptr<IGroupPathResolver> pathResolver() const override;

    /// Change the JSON object that receives ungrouped @c writeRootString
    /// calls. Defaults to @c "General". Call before the first write.
    void setRootGroupName(const QString& name);
    QString rootGroupName() const;

    /// Stamp-on-sync: if non-empty, every @c sync() that writes the file
    /// will insert @p key = @p version when the key is absent from the
    /// root. Used by consumers who pair a @c JsonBackend with a @c Schema
    /// so fresh stores carry the current version from day one. Default
    /// disables the behaviour (empty key).
    void setVersionStamp(const QString& key, int version) override;

    /// Currently-installed version stamp as @c {key, version}. Returns an
    /// empty key when no stamp is installed. Used by shared-backend safety
    /// checks (see @c Store::Store) to detect a clobber.
    std::pair<QString, int> versionStamp() const override;

    /// Run @p schema's migration chain against the in-memory root, and
    /// if any step bumps the version stamp, atomically rewrite the backing
    /// file and refresh the in-memory state. Returns @c true unless the
    /// atomic disk rewrite fails — in which case the backend stays at the
    /// unmigrated state and the next successful @c sync() retries.
    /// Consumers that need the "did migrations actually apply" bit should
    /// check @c versionStamp() before and after, not this return value.
    bool applyMigration(const Schema& schema) override;

    /// Atomically write @p root to @p filePath (temp file + rename via
    /// @c QSaveFile). Exposed for use by @c MigrationRunner and other
    /// external callers that already have a @c QJsonObject in hand.
    /// Returns @c true on success.
    static bool writeJsonAtomically(const QString& filePath, const QJsonObject& root);

    /// Read-only access to the in-memory document. Implicitly shared (COW),
    /// so the copy is cheap and isolated from future mutations.
    QJsonObject jsonRootSnapshot() const;

    /// Backing file path (useful for migration-chain callers that need
    /// to rewrite via @c writeJsonAtomically during schema upgrades).
    QString filePath() const;

    /// Clear the dirty flag without writing. Used by tests + async I/O
    /// that commits snapshots off-thread.
    void clearDirty();

    /// Replace the in-memory document with @p root. Marks the backend
    /// dirty — callers who paired this with a successful
    /// @c writeJsonAtomically may call @c clearDirty() immediately after
    /// to restore the "in-memory matches disk" invariant. Must not be
    /// called while any @c JsonGroup views are alive.
    void replaceRoot(QJsonObject root);

    /// Sync timing policy. See @c setSyncPolicy for the full contract.
    enum class SyncPolicy {
        /// @c sync() flushes the dirty root to disk synchronously and
        /// returns the commit result. Default — matches the pre-policy
        /// behaviour so switching an existing consumer requires no change.
        Synchronous,
        /// @c sync() restarts a single-shot debounce timer and returns
        /// @c true without touching disk. The actual flush happens on the
        /// timer's timeout, on the same thread that owns the backend (i.e.
        /// whichever thread is pumping the event loop that owns the timer).
        /// Callers that need a guaranteed-committed state before proceeding
        /// (e.g. closing a save dialog, shutting down a daemon) call
        /// @c flushPending() to force an immediate flush. The backend's
        /// destructor also flushes pending writes.
        ///
        /// Requires a running event loop to actually fire. Tests that
        /// exercise this path must call @c QTest::qWait() or
        /// @c QCoreApplication::processEvents() after @c sync() to let the
        /// timer elapse, or call @c flushPending() directly.
        Deferred,
    };

    /// Configure when @c sync() commits to disk. @p debounceMs is only
    /// consulted for @c Deferred policy; it sets the coalescing window
    /// during which repeated @c sync() calls restart the same timer.
    /// Values <= 0 are clamped to 1 ms.
    ///
    /// Safe to call at any time. Switching from @c Deferred back to
    /// @c Synchronous also flushes any pending deferred write so the
    /// in-memory and on-disk state agree on return.
    void setSyncPolicy(SyncPolicy policy, int debounceMs = 500);
    SyncPolicy syncPolicy() const;

    /// Flush any pending deferred-sync write to disk now. No-op when the
    /// policy is @c Synchronous or when the backend is not dirty.
    /// Returns the commit result (or @c true when there was nothing to
    /// flush). Safe to call from any context that would otherwise be
    /// able to call @c sync().
    bool flushPending();

private:
    friend class JsonGroup;

    void loadFromDisk();
    void markDirty();
    /// Synchronous inner flush used by @c sync() (Synchronous policy),
    /// @c flushPending(), the deferred-sync timer callback, and the
    /// destructor. No-op when not dirty; returns the @c writeJsonAtomically
    /// result otherwise.
    bool flushNow();
    void incActiveGroupCount();
    void decActiveGroupCount();
    int activeGroupCount() const;

    /// Per-instance warn-once dedup. Returns true the first time
    /// @c tag+@c key is observed on this backend, false on every subsequent
    /// call. Stores entries up to an internal cap; beyond the cap every
    /// unique combination fires its warning (safer than silent corruption).
    bool shouldWarnOnce(const char* tag, const QString& key);

    struct Data;
    std::unique_ptr<Data> d;
};

} // namespace PhosphorConfig
