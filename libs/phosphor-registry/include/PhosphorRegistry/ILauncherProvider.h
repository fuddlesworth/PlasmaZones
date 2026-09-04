// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

namespace PhosphorRegistry {

// One row a launcher provider contributes for the current query.
//
// A plain value: the launcher surface copies these into its own ranked
// model, so a provider may rebuild its list wholesale on every query
// without anything holding references into it. `id` is the handle the
// surface passes back to activate(); it only has to be unique within the
// provider that issued it.
struct PHOSPHORREGISTRY_EXPORT LauncherResult
{
    Q_GADGET
    Q_PROPERTY(QString id MEMBER id)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString subtitle MEMBER subtitle)
    Q_PROPERTY(QString iconName MEMBER iconName)
    Q_PROPERTY(int score MEMBER score)
    Q_PROPERTY(QString primaryActionLabel MEMBER primaryActionLabel)
    Q_PROPERTY(QString alternateActionLabel MEMBER alternateActionLabel)

public:
    QString id;
    QString title;
    QString subtitle;
    // Freedesktop icon name, resolved by the surface through the icon
    // theme. Empty means the surface draws the provider's own glyph.
    QString iconName;
    // Ranking score, higher is better. Providers that fuzzy-match should
    // return the matcher's score so rows from different providers rank
    // against each other on one scale; a provider that answers a query
    // exactly (the calculator) should return a large fixed score.
    int score = 0;
    // What Enter does ("Open", "Run", "Copy"). Shown beside the selected row.
    QString primaryActionLabel;
    // What Alt+Enter does, or empty when the row has no second action.
    QString alternateActionLabel;

    [[nodiscard]] bool hasAlternateAction() const
    {
        return !alternateActionLabel.isEmpty();
    }

    // So a provider can tell whether a recompute actually changed anything
    // before announcing it. Without this every provider emits on every
    // recompute, and each emission costs the model a full reset, which drops
    // the surface's selected row.
    [[nodiscard]] bool operator==(const LauncherResult& other) const = default;
};

// The provider contract the launcher surface drives. This is the concrete
// type ILauncherProviderFactory::createProvider() promised in Phase 1.3.
//
// A provider is a pure data source: it is handed the query and answers
// with rows. It owns no UI. The surface owns ranking across providers,
// grouping, selection and keyboard handling.
//
// Query flow is push-then-pull. setQuery() hands over the new text and
// returns; the provider recomputes (synchronously, or asynchronously for
// one that has to hit a bus or a disk) and emits resultsChanged() whenever
// results() would now answer differently, including for the query that
// was just set. A synchronous provider emits it from inside setQuery.
// The surface reads results() on that signal and never caches across
// queries.
class PHOSPHORREGISTRY_EXPORT ILauncherProvider : public QObject
{
    Q_OBJECT

public:
    // What the user asked for. Primary is Enter; Alternate is Alt+Enter
    // and only valid for a result whose alternateActionLabel is set.
    enum class Activation {
        Primary,
        Alternate,
    };
    Q_ENUM(Activation)

    explicit ILauncherProvider(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~ILauncherProvider() override = default;

    // Stable id, matching the factory that created this provider.
    [[nodiscard]] virtual QString id() const = 0;
    // Header / pill text ("Apps", "Windows").
    [[nodiscard]] virtual QString displayName() const = 0;
    // Glyph for the header and for rows that supply no icon of their own.
    [[nodiscard]] virtual QString iconName() const = 0;

    // Whether this provider has anything to say for an EMPTY query. Most
    // do not (Spotlight shows nothing until typed); a window switcher or
    // clipboard history reasonably lists everything.
    [[nodiscard]] virtual bool listsOnEmptyQuery() const
    {
        return false;
    }

    virtual void setQuery(const QString& query) = 0;
    [[nodiscard]] virtual QList<LauncherResult> results() const = 0;

    // Perform the action for `resultId`. Returns false when the id is
    // unknown, the action is not offered, or the action failed to start;
    // the surface then stays open rather than closing on a no-op.
    [[nodiscard]] virtual bool activate(const QString& resultId, Activation activation) = 0;

Q_SIGNALS:
    void resultsChanged();
};

} // namespace PhosphorRegistry

Q_DECLARE_METATYPE(PhosphorRegistry::LauncherResult)
