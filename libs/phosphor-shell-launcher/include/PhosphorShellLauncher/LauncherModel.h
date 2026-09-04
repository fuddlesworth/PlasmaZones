// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace PhosphorShellLauncher {

// The one model the launcher surface binds: every provider's rows for
// the current query, ranked, grouped by provider, filterable to one.
//
// Ranking is by score within a provider, and providers are ordered by
// their best row, so the section with the most relevant hit comes first
// while rows stay grouped under their provider header (the mockup's
// layout). Across providers the scores are comparable because every
// fuzzy-matching provider scores through the same FuzzyMatcher, and an
// exact-answer provider uses FuzzyMatcher::perfectScore to place itself.
//
// Owns no providers. A host creates them (through the registry) and
// addProvider()s each; the model connects to resultsChanged and rebuilds.
// It is registry-agnostic so a test can feed it any ILauncherProvider.
class PHOSPHORSHELLLAUNCHER_EXPORT LauncherModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // Empty shows every provider; a provider id shows only that one.
    // The pills in the surface set this; Tab cycles it.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString providerFilter READ providerFilter WRITE setProviderFilter NOTIFY providerFilterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // One entry per registered provider, in registration order, each a
    // map {id, name, iconName, count} where count is that provider's rows
    // for the current query (before the filter). For the pill strip.
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        IconNameRole,
        ProviderIdRole,
        ProviderNameRole,
        ProviderIconRole,
        ResultIdRole,
        PrimaryActionLabelRole,
        AlternateActionLabelRole,
        HasAlternateActionRole,
        ScoreRole,
    };
    Q_ENUM(Role)

    explicit LauncherModel(QObject* parent = nullptr);
    ~LauncherModel() override;

    // Not owned. Registration order is the pill order and the tie-break
    // between providers with equal best scores.
    void addProvider(PhosphorRegistry::ILauncherProvider* provider);
    [[nodiscard]] QList<PhosphorRegistry::ILauncherProvider*> providerObjects() const;

    [[nodiscard]] QString query() const;
    void setQuery(const QString& query);

    /// Whether the surface showing this model is on screen.
    ///
    /// Providers stay subscribed to their sources for the whole session,
    /// because a launcher that rescanned on open would be slow exactly when
    /// the user is waiting. That means every clipboard copy and every window
    /// opening drives a recompute, and each one used to rebuild the model
    /// too: a full reset, destroying every delegate, for a surface nobody is
    /// looking at. While inactive the rebuild is deferred, and one runs when
    /// the surface comes back.
    ///
    /// Defaults to true, so a host that never sets it behaves as before.
    [[nodiscard]] bool active() const;
    void setActive(bool active);

    [[nodiscard]] QString providerFilter() const;
    void setProviderFilter(const QString& providerId);
    // Step the filter through "all" and each provider that has rows, in
    // order; +1 forward, -1 back. Tab and Shift+Tab.
    Q_INVOKABLE void cycleProviderFilter(int direction);

    [[nodiscard]] QVariantList providers() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Perform row's primary (or alternate) action. False when the row is
    // out of range, the alternate was asked for but not offered, or the
    // provider refused; the surface stays open in that case.
    [[nodiscard]] Q_INVOKABLE bool activate(int row, bool alternate = false);

Q_SIGNALS:
    void queryChanged();
    void activeChanged();
    void providerFilterChanged();
    void countChanged();
    void providersChanged();

private:
    struct Row
    {
        PhosphorRegistry::ILauncherProvider* provider;
        PhosphorRegistry::LauncherResult result;
    };
    void rebuild();

    QList<PhosphorRegistry::ILauncherProvider*> m_providers;
    QString m_query;
    // Up only while setQuery is pushing the query to every provider, so
    // their synchronous answers coalesce into one rebuild instead of one
    // each. See setQuery.
    bool m_batchingQuery = false;
    bool m_active = true;
    // A rebuild that was skipped because the surface was not on screen.
    bool m_rebuildDeferred = false;
    QString m_providerFilter;
    QList<Row> m_rows;
    // Per-provider row counts for the current query, unfiltered.
    QHash<QString, int> m_counts;
};

} // namespace PhosphorShellLauncher
