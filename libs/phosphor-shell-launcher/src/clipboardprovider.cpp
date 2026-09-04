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
    m_previewRole = role("preview");
    m_mimeRole = role("mimeType");
    m_timestampRole = role("timestamp");
    // The role names are the whole duck-typed contract with the service. A
    // model missing any of them would yield invalid data for every row, and
    // an empty result id in particular would make an activation act on the
    // wrong entry. Go inert loudly instead, matching WindowsProvider.
    if (m_previewRole < 0 || m_mimeRole < 0 || m_timestampRole < 0) {
        qCWarning(lcClipboard) << "clipboard history model does not expose the preview/mimeType/timestamp roles; "
                                  "provider is inert";
        m_history = nullptr;
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
    connect(m_history, &QAbstractItemModel::rowsMoved, this, refresh);
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
    // See AppsProvider::recompute: this runs on every clipboard change for
    // the whole session, including while the launcher is closed, and an
    // unconditional emission would reset the model each time.
    const QList<LauncherResult> previous = std::move(m_results);
    m_results.clear();
    if (m_history) {
        const int rows = m_history->rowCount();
        // Every row is scored; the cap is applied after ranking below.
        // Stopping the scan at the cap would keep the first N matches in
        // history order and discard a better match further back, which is the
        // opposite of what a ranked list should do, and differs from how the
        // sibling providers treat the same constant.
        for (int row = 0; row < rows; ++row) {
            const QModelIndex idx = m_history->index(row, 0);
            const QString preview = m_history->data(idx, m_previewRole).toString();
            int score = 0;
            if (!m_query.isEmpty()) {
                const auto m = FuzzyMatcher::match(m_query, preview, FuzzyMatcher::patternIsCaseSensitive(m_query));
                if (!m) {
                    continue;
                }
                score = m->score;
            }
            LauncherResult r;
            // Timestamp AND row, because two entries captured in the same
            // instant render the same timestamp: a shared id makes rowFor
            // resolve both to whichever comes first, so activating the
            // second copied the first. The row disambiguates within one
            // rendering of the history, which is exactly the window an
            // activation lives in, and rowFor re-derives the same pair.
            r.id = m_history->data(idx, m_timestampRole).toString() + u'#' + QString::number(row);
            // A single line for the row; the entry itself keeps its
            // newlines. Simplified here rather than trusted from the model:
            // the "preview" role is a duck-typed contract with no guarantee
            // that a future service, or a test's fake, collapses whitespace.
            r.title = preview.simplified();
            r.subtitle = m_history->data(idx, m_mimeRole).toString();
            r.iconName = iconName();
            r.score = score;
            r.primaryActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Copy");
            r.alternateActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Remove from history");
            // Pruning is something a user does several times in a row.
            r.alternateIsRepeatable = true;
            m_results.append(std::move(r));
        }
        if (!m_query.isEmpty()) {
            std::stable_sort(m_results.begin(), m_results.end(), [](const LauncherResult& a, const LauncherResult& b) {
                return a.score > b.score;
            });
        }
        if (m_results.size() > m_maximumResults) {
            m_results.resize(m_maximumResults);
        }
    }
    if (m_results == previous) {
        return;
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
    const int rows = m_history->rowCount();
    // The id is "<timestamp>#<row as it was when the row was built>". The
    // timestamp is what identifies the ENTRY, since history shifts under the
    // user between typing and Enter; the row only breaks a tie between two
    // entries captured in the same instant.
    const qsizetype hash = resultId.lastIndexOf(u'#');
    const QString stamp = hash < 0 ? resultId : resultId.left(hash);
    const int hintedRow = hash < 0 ? -1 : resultId.mid(hash + 1).toInt();
    int firstMatch = -1;
    for (int row = 0; row < rows; ++row) {
        if (m_history->data(m_history->index(row, 0), m_timestampRole).toString() != stamp) {
            continue;
        }
        if (row == hintedRow) {
            return row;
        }
        if (firstMatch < 0) {
            firstMatch = row;
        }
    }
    return firstMatch;
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
    // The service reports whether it actually did the thing. Without that
    // return, an entry whose content or mime type is empty was silently
    // ignored while this reported success, and the surface closed on a copy
    // that never happened.
    bool done = false;
    if (!QMetaObject::invokeMethod(m_service, method, Q_RETURN_ARG(bool, done), Q_ARG(int, row))) {
        qCWarning(lcClipboard) << "activate: clipboard service has no bool" << method << "(int)";
        return false;
    }
    if (!done) {
        qCWarning(lcClipboard) << "activate: clipboard service refused" << method << "for row" << row;
    }
    return done;
}

} // namespace PhosphorShellLauncher
