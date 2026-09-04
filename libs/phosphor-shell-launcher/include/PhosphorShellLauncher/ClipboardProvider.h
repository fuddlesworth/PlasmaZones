// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QPointer>
#include <QString>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

namespace PhosphorShellLauncher {

// Clipboard history, from phosphor-service-clipboard's history model.
// Lists everything on an empty query (most recent first, as the model
// orders it) and fuzzy-matches the preview text otherwise. Enter copies
// the entry back to the clipboard; Alt+Enter removes it from history.
//
// The service is reached through its `history` QAbstractItemModel and
// the copy/remove slots by name, not through ClipboardService's C++
// type, so this library does not link phosphor-service-clipboard: a host
// that has no clipboard service simply does not register this provider.
// The role names ("preview", "mimeType", "timestamp") are the contract.
//
// Rows are addressed by the entry's timestamp rather than its index,
// because history shifts under the user between typing and Enter (a copy
// elsewhere inserts a row at the top); an index captured at query time
// would then act on the wrong entry.
class PHOSPHORSHELLLAUNCHER_EXPORT ClipboardProvider : public PhosphorRegistry::ILauncherProvider
{
    Q_OBJECT

public:
    // `service` must expose `history` (QAbstractItemModel*), `copy(int)`
    // and `remove(int)`. Null makes the provider inert (no rows), never
    // a crash.
    explicit ClipboardProvider(QObject* service, QObject* parent = nullptr);
    ~ClipboardProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString iconName() const override;
    [[nodiscard]] bool listsOnEmptyQuery() const override;

    void setQuery(const QString& query) override;
    [[nodiscard]] QList<PhosphorRegistry::LauncherResult> results() const override;
    [[nodiscard]] bool activate(const QString& resultId, Activation activation) override;

    void setMaximumResults(int count);
    [[nodiscard]] int maximumResults() const;

private:
    void recompute();
    [[nodiscard]] int rowFor(const QString& resultId) const;
    [[nodiscard]] int role(const char* name) const;

    QPointer<QObject> m_service;
    QPointer<QAbstractItemModel> m_history;
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
    int m_maximumResults = 24;
};

} // namespace PhosphorShellLauncher
