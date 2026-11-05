// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QDBusServiceWatcher;
QT_END_NAMESPACE

namespace PhosphorShellDashboard {

/**
 * @brief The daemon's bound chords, for the cheatsheet (A3 §10).
 *
 *     import Phosphor.Dashboard
 *     ShortcutCatalog { id: chords }
 *     Cheatsheet { catalog: chords.rows }
 *
 * Reads `org.plasmazones.Control.getShortcutsJson` when the daemon is on
 * the bus and again on its `shortcutsChanged`, so a rebind (in the
 * settings or through the compositor) reaches an open sheet live. `rows`
 * is the JSON array as a list of maps, one per action: `id, label,
 * description, category, categoryOrder, rowOrder, triggers (string
 * list), assigned, mode`. Empty while the daemon is absent or on an
 * older daemon without the method.
 */
class ShortcutCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// Whether the daemon is on the session bus right now.
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)

public:
    explicit ShortcutCatalog(QObject* parent = nullptr);
    ~ShortcutCatalog() override;

    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QVariantList rows() const;

public Q_SLOTS:
    /// Re-read the catalog now. Async; `rowsChanged` fires when it differs.
    /// A slot, so the daemon's `shortcutsChanged` can be wired to it by name.
    void refresh();

public:
    /// The wire document as rows: a JSON array of objects, each kept as a
    /// map with `triggers` normalised to a string list. Anything else
    /// (a malformed document, a non-object entry) parses to nothing.
    [[nodiscard]] static QVariantList parseRows(const QString& json);

Q_SIGNALS:
    void availableChanged();
    void rowsChanged();

private:
    void setAvailable(bool available);
    void setRows(const QVariantList& rows);

    QDBusServiceWatcher* m_watcher = nullptr;
    QVariantList m_rows;
    bool m_available = false;
    // Bumped per refresh(); a reply from an older read is dropped.
    int m_generation = 0;
};

} // namespace PhosphorShellDashboard
