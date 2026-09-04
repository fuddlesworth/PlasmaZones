// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/ClipboardProvider.h>

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QVariant>

#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(lcClipboard, "phosphor.launcher.clipboard")
}

namespace PhosphorShellLauncher {

using PhosphorRegistry::LauncherResult;

ClipboardProvider::ClipboardProvider(QObject* service, QObject* parent)
    : ILauncherProvider(parent)
    , m_service(service)
{
    if (m_service) {
        m_history = m_service->property("history").value<QAbstractItemModel*>();
    }
    if (!m_history) {
        qCWarning(lcClipboard) << "no clipboard history model; provider is inert";
        return;
    }
    // Any change to history changes the answer to the current query, most
    // visibly for the empty query that lists everything.
    const auto refresh = [this] {
        recompute();
    };
    connect(m_history, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(m_history, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(m_history, &QAbstractItemModel::modelReset, this, refresh);
    connect(m_history, &QAbstractItemModel::dataChanged, this, refresh);
    connect(m_history, &QAbstractItemModel::layoutChanged, this, refresh);
}

ClipboardProvider::~ClipboardProvider() = default;

QString ClipboardProvider::id() const
{
    return QStringLiteral("clipboard");
}

QString ClipboardProvider::displayName() const
{
    return QCoreApplication::translate("PhosphorShellLauncher", "Clipboard");
}

QString ClipboardProvider::iconName() const
{
    return QStringLiteral("edit-paste");
}

bool ClipboardProvider::listsOnEmptyQuery() const
{
    return true;
}

void ClipboardProvider::setMaximumResults(int count)
{
    m_maximumResults = std::max(1, count);
}

int ClipboardProvider::maximumResults() const
{
    return m_maximumResults;
}

int ClipboardProvider::role(const char* name) const
{
    if (!m_history) {
        return -1;
    }
    const QHash<int, QByteArray> roles = m_history->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == name) {
            return it.key();
        }
    }
    return -1;
}

void ClipboardProvider::setQuery(const QString& query)
{
    m_query = query;
    recompute();
}

void ClipboardProvider::recompute()
{
    m_results.clear();
    if (m_history) {
        const int previewRole = role("preview");
        const int mimeRole = role("mimeType");
        const int timestampRole = role("timestamp");
        const int rows = m_history->rowCount();
        for (int row = 0; row < rows && m_results.size() < m_maximumResults; ++row) {
            const QModelIndex idx = m_history->index(row, 0);
            const QString preview = m_history->data(idx, previewRole).toString();
            int score = 0;
            if (!m_query.isEmpty()) {
                const auto m = FuzzyMatcher::match(m_query, preview);
                if (!m) {
                    continue;
                }
                score = m->score;
            }
            LauncherResult r;
            r.id = m_history->data(idx, timestampRole).toString();
            // A single line for the row; the entry itself keeps its newlines.
            r.title = preview.simplified();
            r.subtitle = m_history->data(idx, mimeRole).toString();
            r.iconName = iconName();
            r.score = score;
            r.primaryActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Copy");
            r.alternateActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Remove from history");
            m_results.append(std::move(r));
        }
        if (!m_query.isEmpty()) {
            std::stable_sort(m_results.begin(), m_results.end(), [](const LauncherResult& a, const LauncherResult& b) {
                return a.score > b.score;
            });
        }
    }
    Q_EMIT resultsChanged();
}

QList<LauncherResult> ClipboardProvider::results() const
{
    return m_results;
}

int ClipboardProvider::rowFor(const QString& resultId) const
{
    if (!m_history) {
        return -1;
    }
    const int timestampRole = role("timestamp");
    const int rows = m_history->rowCount();
    for (int row = 0; row < rows; ++row) {
        if (m_history->data(m_history->index(row, 0), timestampRole).toString() == resultId) {
            return row;
        }
    }
    return -1;
}

bool ClipboardProvider::activate(const QString& resultId, Activation activation)
{
    if (!m_service) {
        return false;
    }
    // Re-resolve at activation time; see the header for why not an index.
    const int row = rowFor(resultId);
    if (row < 0) {
        qCWarning(lcClipboard) << "activate: entry no longer in history" << resultId;
        return false;
    }
    const char* method = activation == Activation::Primary ? "copy" : "remove";
    if (!QMetaObject::invokeMethod(m_service, method, Q_ARG(int, row))) {
        qCWarning(lcClipboard) << "activate: clipboard service has no" << method << "(int)";
        return false;
    }
    return true;
}

} // namespace PhosphorShellLauncher
