// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/LauncherModel.h>

#include <QLoggingCategory>
#include <QVariantMap>

#include <algorithm>

namespace {
// Every provider logs its own activation refusals; without this the model's
// own refusals were the one silent branch in the chain, so a user pressing
// Enter and seeing nothing happen left no trace anywhere.
Q_LOGGING_CATEGORY(lcLauncherModel, "phosphor.launcher.model")
} // namespace

namespace PhosphorShellLauncher {

using PhosphorRegistry::ILauncherProvider;
using PhosphorRegistry::LauncherResult;

LauncherModel::LauncherModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

LauncherModel::~LauncherModel() = default;

void LauncherModel::addProvider(ILauncherProvider* provider)
{
    if (!provider || m_providers.contains(provider)) {
        return;
    }
    m_providers.append(provider);
    connect(provider, &ILauncherProvider::resultsChanged, this, &LauncherModel::rebuild);
    // Not owned, but a provider destroyed under us must drop out of the
    // list rather than dangle into the next rebuild.
    connect(provider, &QObject::destroyed, this, [this, provider] {
        m_providers.removeAll(provider);
        rebuild();
    });
    provider->setQuery(m_query);
    Q_EMIT providersChanged();
}

QList<ILauncherProvider*> LauncherModel::providerObjects() const
{
    return m_providers;
}

QString LauncherModel::query() const
{
    return m_query;
}

void LauncherModel::setQuery(const QString& query)
{
    if (m_query == query) {
        return;
    }
    m_query = query;
    Q_EMIT queryChanged();
    // Batched. Every provider answers a query synchronously, and each
    // answer used to rebuild on its own: five providers meant five full
    // model resets per keystroke, each destroying every delegate and
    // resetting the current row, so the selection jumped while the user
    // typed. The flag suppresses the per-provider rebuilds and one runs
    // after the last provider has answered.
    //
    // An asynchronous provider answering later still rebuilds on its own,
    // because the flag is only up for the duration of this loop.
    m_batchingQuery = true;
    for (ILauncherProvider* provider : std::as_const(m_providers)) {
        provider->setQuery(m_query);
    }
    m_batchingQuery = false;
    rebuild();
}

bool LauncherModel::active() const
{
    return m_active;
}

void LauncherModel::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();
    if (m_active && m_rebuildDeferred) {
        rebuild();
    }
}

QString LauncherModel::providerFilter() const
{
    return m_providerFilter;
}

void LauncherModel::setProviderFilter(const QString& providerId)
{
    if (m_providerFilter == providerId) {
        return;
    }
    m_providerFilter = providerId;
    Q_EMIT providerFilterChanged();
    rebuild();
}

void LauncherModel::cycleProviderFilter(int direction)
{
    // The cycle is "all", then each provider that currently has rows, in
    // registration order. Providers with nothing to show are skipped so
    // Tab never lands on an empty list.
    QStringList ring{QString()};
    for (ILauncherProvider* provider : std::as_const(m_providers)) {
        if (m_counts.value(provider->id()) > 0) {
            ring.append(provider->id());
        }
    }
    if (ring.size() <= 1) {
        setProviderFilter(QString());
        return;
    }
    const int at = ring.indexOf(m_providerFilter);
    if (at < 0) {
        // Defensive. rebuild() clears a filter whose provider has no rows,
        // and the ring is built from the same counts, so the two agree and
        // this should be unreachable through the public API. Kept because
        // the alternative, treating a missing filter as position 0, steps to
        // the FIRST provider on Tab and the LAST on Shift+Tab, skipping
        // "all" in both directions, which is a silent wrong answer rather
        // than an obvious one.
        setProviderFilter(QString());
        return;
    }
    const int step = direction < 0 ? -1 : 1;
    const int n = static_cast<int>(ring.size());
    setProviderFilter(ring.at(((at + step) % n + n) % n));
}

QVariantList LauncherModel::providers() const
{
    QVariantList out;
    for (ILauncherProvider* provider : std::as_const(m_providers)) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), provider->id());
        entry.insert(QStringLiteral("name"), provider->displayName());
        entry.insert(QStringLiteral("iconName"), provider->iconName());
        // What the provider OFFERED, not how many things matched. Every
        // provider caps its own result list before the model sees it, so a
        // query matching three hundred applications reports the cap. The
        // pill is a "there is more here" affordance rather than a tally, and
        // presenting the true total would promise rows the user cannot
        // reach by selecting that pill.
        entry.insert(QStringLiteral("count"), m_counts.value(provider->id()));
        out.append(entry);
    }
    return out;
}

void LauncherModel::rebuild()
{
    if (m_batchingQuery) {
        return;
    }
    if (!m_active) {
        // Nobody is looking. Remember that the rows are stale and rebuild
        // once the surface comes back, rather than resetting the model for
        // every clipboard copy and every window that opens all session.
        m_rebuildDeferred = true;
        return;
    }
    m_rebuildDeferred = false;
    // Gather per provider, then order providers by their best row.
    struct Group
    {
        ILauncherProvider* provider;
        QList<LauncherResult> results;
        int best;
        int order;
    };
    QList<Group> groups;
    QHash<QString, int> counts;
    int order = 0;
    for (ILauncherProvider* provider : std::as_const(m_providers)) {
        ++order;
        if (m_query.isEmpty() && !provider->listsOnEmptyQuery()) {
            counts.insert(provider->id(), 0);
            continue;
        }
        QList<LauncherResult> results = provider->results();
        counts.insert(provider->id(), static_cast<int>(results.size()));
        if (results.isEmpty()) {
            continue;
        }
        std::stable_sort(results.begin(), results.end(), [](const LauncherResult& a, const LauncherResult& b) {
            return a.score > b.score;
        });
        groups.append({provider, std::move(results), 0, order});
        groups.last().best = groups.last().results.first().score;
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        if (a.best != b.best) {
            return a.best > b.best;
        }
        return a.order < b.order;
    });

    // A filter pinned to a provider that no longer has rows would show an
    // empty list with no way to tell why, and nothing would clear it. Drop
    // it here, before assembling rows, so the surface falls back to "all".
    bool filterCleared = false;
    if (!m_providerFilter.isEmpty() && counts.value(m_providerFilter) == 0) {
        m_providerFilter.clear();
        filterCleared = true;
    }

    QList<Row> rows;
    for (Group& group : groups) {
        if (!m_providerFilter.isEmpty() && group.provider->id() != m_providerFilter) {
            continue;
        }
        for (LauncherResult& result : group.results) {
            rows.append({group.provider, std::move(result)});
        }
    }

    const bool countsChanged = counts != m_counts;
    const int oldCount = static_cast<int>(m_rows.size());
    beginResetModel();
    m_rows = std::move(rows);
    m_counts = std::move(counts);
    endResetModel();
    if (oldCount != m_rows.size()) {
        Q_EMIT countChanged();
    }
    if (countsChanged) {
        Q_EMIT providersChanged();
    }
    if (filterCleared) {
        Q_EMIT providerFilterChanged();
    }
}

int LauncherModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant LauncherModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row& row = m_rows.at(index.row());
    switch (role) {
    case TitleRole:
        return row.result.title;
    case SubtitleRole:
        return row.result.subtitle;
    case IconNameRole:
        return row.result.iconName.isEmpty() ? row.provider->iconName() : row.result.iconName;
    case ProviderIdRole:
        return row.provider->id();
    case ProviderNameRole:
        return row.provider->displayName();
    case ProviderIconRole:
        return row.provider->iconName();
    case ResultIdRole:
        return row.result.id;
    case PrimaryActionLabelRole:
        return row.result.primaryActionLabel;
    case AlternateActionLabelRole:
        return row.result.alternateActionLabel;
    case HasAlternateActionRole:
        return row.result.hasAlternateAction();
    case ScoreRole:
        return row.result.score;
    default:
        return {};
    }
}

QHash<int, QByteArray> LauncherModel::roleNames() const
{
    return {
        {TitleRole, QByteArrayLiteral("title")},
        {SubtitleRole, QByteArrayLiteral("subtitle")},
        {IconNameRole, QByteArrayLiteral("iconName")},
        {ProviderIdRole, QByteArrayLiteral("providerId")},
        {ProviderNameRole, QByteArrayLiteral("providerName")},
        {ProviderIconRole, QByteArrayLiteral("providerIcon")},
        {ResultIdRole, QByteArrayLiteral("resultId")},
        {PrimaryActionLabelRole, QByteArrayLiteral("primaryActionLabel")},
        {AlternateActionLabelRole, QByteArrayLiteral("alternateActionLabel")},
        {HasAlternateActionRole, QByteArrayLiteral("hasAlternateAction")},
        {ScoreRole, QByteArrayLiteral("score")},
    };
}

bool LauncherModel::activate(int row, bool alternate)
{
    // Copy out before calling. A provider's activate can drive its source
    // model synchronously (the clipboard's remove does), which re-enters
    // rebuild() and reassigns m_rows, destroying the row this reference and
    // its id point into while the call is still on the stack.
    if (row < 0 || row >= m_rows.size()) {
        qCDebug(lcLauncherModel) << "activate: row" << row << "out of range (" << m_rows.size() << "rows )";
        return false;
    }
    ILauncherProvider* const provider = m_rows.at(row).provider;
    const QString resultId = m_rows.at(row).result.id;
    const bool hasAlternate = m_rows.at(row).result.hasAlternateAction();
    if (alternate && !hasAlternate) {
        qCDebug(lcLauncherModel) << "activate: row" << row << "offers no alternate action";
        return false;
    }
    return provider->activate(
        resultId, alternate ? ILauncherProvider::Activation::Alternate : ILauncherProvider::Activation::Primary);
}

bool LauncherModel::alternateIsRepeatable(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return false;
    }
    return m_rows.at(row).result.alternateIsRepeatable;
}

} // namespace PhosphorShellLauncher
