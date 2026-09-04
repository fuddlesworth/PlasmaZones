// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/WindowsProvider.h>

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QVariant>

#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(lcWindows, "phosphor.launcher.windows")
}

namespace PhosphorShellLauncher {

using PhosphorRegistry::LauncherResult;

WindowsProvider::WindowsProvider(QAbstractItemModel* toplevels, QObject* parent)
    : ILauncherProvider(parent)
    , m_toplevels(toplevels)
{
    if (!m_toplevels) {
        qCWarning(lcWindows) << "no toplevel model; provider is inert";
        return;
    }
    const QHash<int, QByteArray> roles = m_toplevels->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == "toplevel") {
            m_toplevelRole = it.key();
        }
    }
    if (m_toplevelRole < 0) {
        qCWarning(lcWindows) << "toplevel model has no 'toplevel' role; provider is inert";
        m_toplevels = nullptr;
        return;
    }
    const auto refresh = [this] {
        recompute();
    };
    connect(m_toplevels, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(m_toplevels, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(m_toplevels, &QAbstractItemModel::modelReset, this, refresh);
    connect(m_toplevels, &QAbstractItemModel::dataChanged, this, refresh);
    // A model that reorders through moveRows or layoutChanged rather than
    // insert/remove would otherwise leave the cached rows stale.
    connect(m_toplevels, &QAbstractItemModel::rowsMoved, this, refresh);
    connect(m_toplevels, &QAbstractItemModel::layoutChanged, this, refresh);
}

WindowsProvider::~WindowsProvider() = default;

QString WindowsProvider::id() const
{
    return QStringLiteral("windows");
}

QString WindowsProvider::displayName() const
{
    return QCoreApplication::translate("PhosphorShellLauncher", "Windows");
}

QString WindowsProvider::iconName() const
{
    return QStringLiteral("preferences-system-windows");
}

bool WindowsProvider::listsOnEmptyQuery() const
{
    return true;
}

QObject* WindowsProvider::toplevelAt(int row) const
{
    if (!m_toplevels) {
        return nullptr;
    }
    return m_toplevels->data(m_toplevels->index(row, 0), m_toplevelRole).value<QObject*>();
}

QObject* WindowsProvider::toplevelFor(const QString& resultId) const
{
    if (!m_toplevels) {
        return nullptr;
    }
    const int rows = m_toplevels->rowCount();
    for (int row = 0; row < rows; ++row) {
        QObject* obj = toplevelAt(row);
        if (obj && QString::number(reinterpret_cast<quintptr>(obj)) == resultId) {
            return obj;
        }
    }
    return nullptr;
}

void WindowsProvider::setQuery(const QString& query)
{
    m_query = query;
    recompute();
}

void WindowsProvider::recompute()
{
    m_results.clear();
    if (m_toplevels) {
        const int rows = m_toplevels->rowCount();
        for (int row = 0; row < rows; ++row) {
            QObject* obj = toplevelAt(row);
            if (!obj) {
                continue;
            }
            const QString title = obj->property("title").toString();
            const QString appId = obj->property("appId").toString();
            int score = 0;
            if (!m_query.isEmpty()) {
                int best = -1;
                if (const auto m = FuzzyMatcher::match(m_query, title)) {
                    best = m->score;
                }
                if (const auto m = FuzzyMatcher::match(m_query, appId)) {
                    best = std::max(best, m->score);
                }
                if (best < 0) {
                    continue;
                }
                score = best;
            }
            LauncherResult r;
            r.id = QString::number(reinterpret_cast<quintptr>(obj));
            r.title = title.isEmpty() ? appId : title;
            r.subtitle = appId;
            // The app id doubles as the icon name for most desktop apps
            // (org.mozilla.firefox → its icon); the theme resolves what it
            // can and the surface falls back to the provider glyph.
            r.iconName = appId;
            r.score = score;
            r.primaryActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Switch to");
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

QList<LauncherResult> WindowsProvider::results() const
{
    return m_results;
}

bool WindowsProvider::activate(const QString& resultId, Activation activation)
{
    if (activation != Activation::Primary) {
        return false;
    }
    QObject* toplevel = toplevelFor(resultId);
    if (!toplevel) {
        qCWarning(lcWindows) << "activate: window is gone" << resultId;
        return false;
    }
    if (!QMetaObject::invokeMethod(toplevel, "activate")) {
        qCWarning(lcWindows) << "activate: toplevel object has no activate()";
        return false;
    }
    return true;
}

} // namespace PhosphorShellLauncher
