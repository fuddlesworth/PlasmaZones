// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/WindowsProvider.h>

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDir>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaObject>
#include <QVariant>

#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(lcWindows, "phosphor.launcher.windows")

// Window titles come from the client and have no length bound. The matcher's
// cost is proportional to the candidate, and this runs per window per
// keystroke, so truncate to far more than a title needs to be identifiable.
constexpr int kMaxCandidateChars = 512;

// Match the cap the sibling providers apply, so one launcher does not present
// three different result-count policies. Applied AFTER ranking, so the best
// matches survive rather than the first ones found.
constexpr int kMaximumResults = 24;
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
            break;
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

void WindowsProvider::watchToplevels()
{
    if (!m_toplevels) {
        return;
    }
    const int rows = m_toplevels->rowCount();
    for (int row = 0; row < rows; ++row) {
        QObject* obj = toplevelAt(row);
        if (!obj || m_watched.contains(obj)) {
            continue;
        }
        // Connected by NAME rather than to a typed signal, for the same
        // reason the role lookup is by name: this library does not link the
        // Wayland toplevel type, and a host can pass any model whose rows
        // carry these notifications.
        const QMetaObject* meta = obj->metaObject();
        for (const char* signalName : {"titleChanged()", "appIdChanged()"}) {
            const int index = meta->indexOfSignal(signalName);
            if (index < 0) {
                continue;
            }
            connect(obj, meta->method(index), this, metaObject()->method(metaObject()->indexOfSlot("recompute()")));
        }
        connect(obj, &QObject::destroyed, this, [this](QObject* dead) {
            m_watched.remove(dead);
        });
        m_watched.insert(obj);
    }
}

void WindowsProvider::recompute()
{
    watchToplevels();
    // See AppsProvider::recompute: this runs on every window open and close
    // for the whole session, including while the launcher is closed.
    const QList<LauncherResult> previous = std::move(m_results);
    m_results.clear();
    if (m_toplevels) {
        const int rows = m_toplevels->rowCount();
        for (int row = 0; row < rows; ++row) {
            QObject* obj = toplevelAt(row);
            if (!obj) {
                continue;
            }
            // Window titles are client-supplied and unbounded. The matcher
            // allocates matrices proportional to the candidate length on
            // every keystroke, so cap what reaches it; no launcher needs
            // more than this to identify a window.
            const QString title = obj->property("title").toString().left(kMaxCandidateChars);
            const QString appId = obj->property("appId").toString().left(kMaxCandidateChars);
            int score = 0;
            // Smart case: a lower-case query matches anything, and a
            // typed capital means the user wants it.
            const bool smartCase = FuzzyMatcher::patternIsCaseSensitive(m_query);
            if (!m_query.isEmpty()) {
                int best = -1;
                if (const auto m = FuzzyMatcher::match(m_query, title, smartCase)) {
                    best = m->score;
                }
                if (const auto m = FuzzyMatcher::match(m_query, appId, smartCase)) {
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
            // can and the surface falls back to the provider glyph. It is
            // client-controlled, though, and an icon SOURCE that looks like a
            // path is loaded as a file, so only pass through theme-name
            // shaped values.
            r.iconName = (appId.contains(u'/') || QDir::isAbsolutePath(appId)) ? QString() : appId;
            r.score = score;
            r.primaryActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Switch to");
            m_results.append(std::move(r));
        }
        if (!m_query.isEmpty()) {
            std::stable_sort(m_results.begin(), m_results.end(), [](const LauncherResult& a, const LauncherResult& b) {
                return a.score > b.score;
            });
        }
        // Truncate after ranking, never before: capping the scan would drop
        // a better match that happened to sit later in the model.
        if (m_results.size() > kMaximumResults) {
            m_results.resize(kMaximumResults);
        }
    }
    if (m_results == previous) {
        return;
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
    // invokeMethod reports whether the method was CALLED, not whether it did
    // anything: activate() returns void. That is as much as this provider can
    // know, so the true it returns means "the request was delivered to a
    // toplevel that still exists", which is what the row's id was re-resolved
    // for immediately above.
    if (!QMetaObject::invokeMethod(toplevel, "activate")) {
        qCWarning(lcWindows) << "activate: toplevel object has no activate()";
        return false;
    }
    return true;
}

} // namespace PhosphorShellLauncher
