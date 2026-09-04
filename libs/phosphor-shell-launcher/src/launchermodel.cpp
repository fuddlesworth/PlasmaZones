// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/LauncherModel.h>

#include <QVariantMap>

#include <algorithm>

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
    // Each provider emits resultsChanged from setQuery (or later, if it
    // is asynchronous), and each emission rebuilds. That is N rebuilds
    // for N synchronous providers on every keystroke, which is fine at
    // launcher scale and keeps the model honest for async providers with
    // no extra machinery.
    for (ILauncherProvider* provider : std::as_const(m_providers)) {
        provider->setQuery(m_query);
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
    int at = ring.indexOf(m_providerFilter);
    if (at < 0) {
        at = 0;
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
        entry.insert(QStringLiteral("count"), m_counts.value(provider->id()));
        out.append(entry);
    }
    return out;
}

void LauncherModel::rebuild()
{
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
    if (row < 0 || row >= m_rows.size()) {
        return false;
    }
    const Row& r = m_rows.at(row);
    if (alternate && !r.result.hasAlternateAction()) {
        return false;
    }
    return r.provider->activate(
        r.result.id, alternate ? ILauncherProvider::Activation::Alternate : ILauncherProvider::Activation::Primary);
}

} // namespace PhosphorShellLauncher
