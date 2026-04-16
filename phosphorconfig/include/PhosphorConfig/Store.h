// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

#include <memory>

namespace PhosphorConfig {

class IBackend;
struct Schema;

/// High-level declarative configuration facade.
///
/// Wraps an @c IBackend with a @c Schema so consumers get:
///   - Automatic default fallback on reads (from the schema, no re-declared defaults at call sites)
///   - @c reset() / @c resetGroup() / @c resetAll() operations, no hand-written switch statements
///   - @c exportToJson() / @c importFromJson() for backup, dotfile sync, and settings-panel "export"
///   - Automatic schema-version stamping and migration-runner execution on construction
///   - A uniform @c changed() signal that consumers can wire directly to UI updates
///
/// @code
///   Schema schema;
///   schema.version = 1;
///   schema.groups["Window"] = {
///       {QStringLiteral("Width"),  600,       QMetaType::Int},
///       {QStringLiteral("Height"), 400,       QMetaType::Int},
///       {QStringLiteral("Title"),  QStringLiteral("Hello")},
///   };
///
///   auto store = std::make_unique<Store>(std::move(backend), schema);
///   int w = store->read<int>("Window", "Width"); // returns 600 if unset
///   store->write("Window", "Width", 800);
///   store->reset("Window", "Width"); // back to 600
/// @endcode
class PHOSPHORCONFIG_EXPORT Store : public QObject
{
    Q_OBJECT

public:
    /// Construct a store over @p backend governed by @p schema. The schema's
    /// migration chain runs against the backend's current state on entry;
    /// after migration the version key is stamped and the backend synced.
    /// Takes ownership of @p backend.
    explicit Store(std::unique_ptr<IBackend> backend, Schema schema, QObject* parent = nullptr);

    ~Store() override;

    /// Read a declared key. If the value is absent or unparseable, returns
    /// the schema default. Undeclared keys also return the schema default
    /// (which is @c QVariant() — effectively @c T{}).
    template<typename T>
    T read(const QString& group, const QString& key) const;

    /// Read as @c QVariant. Uses @c QString coercion on the wire and
    /// reconstructs the type via @c QMetaType when possible. Undeclared
    /// keys return @c QVariant().
    QVariant readVariant(const QString& group, const QString& key) const;

    /// Write a value. If the schema declares an @c expectedType for this
    /// key and @p value has a different @c typeId, a warning is logged but
    /// the write proceeds (Qt will coerce on read back). Emits @c changed.
    void write(const QString& group, const QString& key, const QVariant& value);

    /// Reset one key to its schema default. No-op when the key is undeclared.
    /// Emits @c changed if the key existed in the backing store.
    void reset(const QString& group, const QString& key);

    /// Reset every key declared in @p group. Undeclared extras are left alone.
    void resetGroup(const QString& group);

    /// Reset every declared key in every declared group. Extras are untouched.
    void resetAll();

    /// Produce a JSON snapshot of every declared key's current value. Useful
    /// for "export settings" UIs and dotfile sync. Keys absent from the
    /// backing store are emitted with their schema default.
    QJsonObject exportToJson() const;

    /// Overwrite declared keys from @p snapshot. Unknown groups/keys in
    /// @p snapshot are ignored (silently — no adaptive migration).
    /// Use @c MigrationRunner first if @p snapshot came from an older schema.
    void importFromJson(const QJsonObject& snapshot);

    /// Flush the underlying backend.
    void sync();

    /// Direct access for callers that still need the backend (e.g. to read
    /// an undeclared key or to call @c groupList()).
    IBackend* backend() const;

    /// The schema the store was constructed with.
    const Schema& schema() const;

Q_SIGNALS:
    /// Emitted after any successful @c write() or @c reset() operation.
    /// Connect to refresh dependent UI.
    void changed(const QString& group, const QString& key);

private:
    class Private;
    std::unique_ptr<Private> d;
};

// ─ Supported read<T> specializations ────────────────────────────────────
// read<T> is fully specialized in store.cpp for QString / int / bool /
// double / QColor — calls with any other T will fail to link. Extend by
// adding a specialization there and re-declaring it below so consumers see
// a declaration without a primary-template body in the header.

template<>
PHOSPHORCONFIG_EXPORT QString Store::read<QString>(const QString&, const QString&) const;
template<>
PHOSPHORCONFIG_EXPORT int Store::read<int>(const QString&, const QString&) const;
template<>
PHOSPHORCONFIG_EXPORT bool Store::read<bool>(const QString&, const QString&) const;
template<>
PHOSPHORCONFIG_EXPORT double Store::read<double>(const QString&, const QString&) const;
template<>
PHOSPHORCONFIG_EXPORT QColor Store::read<QColor>(const QString&, const QString&) const;

} // namespace PhosphorConfig
