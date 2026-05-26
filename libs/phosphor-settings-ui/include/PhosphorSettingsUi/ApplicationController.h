// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "PhosphorSettingsUi/PageRegistry.h"
#include "phosphorsettingsui_export.h"

namespace PhosphorSettingsUi {

class PageController;
class StagingDomain;

/**
 * Top-level orchestrator for a phosphor-settings-ui application.
 *
 * Owns the PageRegistry, the set of registered StagingDomains (including
 * the ones embedded in PageControllers), and the current page selection.
 *
 * Recomputes a global dirty flag whenever any domain's dirtyChanged()
 * fires, drives applyAll() / discardAll() across all domains, and dispatches
 * a per-page resetToDefaults() to the current page only.
 *
 * Apps typically subclass this to declare their pages in the constructor:
 *
 *   auto *page = new MyPage(this);
 *   registerPage(page, {}, tr("My Page"), QUrl("qrc:/MyPage.qml"));
 */
class PHOSPHORSETTINGSUI_EXPORT ApplicationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PhosphorSettingsUi::PageRegistry* registry READ registry CONSTANT)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId NOTIFY currentPageIdChanged)
    QML_NAMED_ELEMENT(ApplicationController)
    QML_UNCREATABLE("ApplicationController is constructed in C++.")

public:
    explicit ApplicationController(QObject* parent = nullptr);
    ~ApplicationController() override;

    PageRegistry* registry() const;

    bool isDirty() const;

    QString currentPageId() const;
    void setCurrentPageId(const QString& id);

    /** Register a page in the sidebar. The page is also tracked as a
     *  staging domain. The controller's parent is reassigned to this
     *  ApplicationController if it has none. */
    void registerPage(PageController* page, const QString& parentId, const QString& title, const QUrl& qmlSource,
                      const QString& iconSource = QString());

    /** Register a headless staging domain (no sidebar entry).
     *  Used for cross-cutting state shared across multiple pages. */
    void registerDomain(StagingDomain* domain);

public Q_SLOTS:
    void applyAll();
    void discardAll();
    void resetCurrentPage();

Q_SIGNALS:
    void dirtyChanged();
    void currentPageIdChanged();

private Q_SLOTS:
    void onDomainDirtyChanged();

private:
    void trackDomain(StagingDomain* domain);
    void recomputeDirty();

    PageRegistry* m_registry;
    QList<QPointer<StagingDomain>> m_domains;
    QString m_currentPageId;
    bool m_dirty = false;
};

} // namespace PhosphorSettingsUi
