// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "phosphorcontrol_export.h"

namespace PhosphorControl {

class PageController;

/**
 * Holds the catalogue of pages an application has registered.
 *
 * Pages are organised as a tree: each entry has an optional parentId.
 * Top-level pages (parentId empty) are the entries the sidebar root
 * displays; child pages are listed under their parent for drill-down.
 *
 * The registry stores QPointer<PageController> weak references — ownership
 * lives with whoever constructed the controller (typically the
 * ApplicationController owning a parent QObject chain), and lookups via
 * controller(id) return nullptr if the controller has been destroyed
 * out-of-order. Registry entries are never removed at runtime; the
 * catalogue is built at application start and is read-only thereafter.
 * Dynamic page registration (post-startup `registerPage` calls) is
 * supported and fires `pageRegistered`; dynamic removal is intentionally
 * NOT — apps that need plugin-style hot-unload should rebuild the
 * controller.
 */
class PHOSPHORCONTROL_EXPORT PageRegistry : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PageRegistry)
    QML_UNCREATABLE("PageRegistry is owned by ApplicationController.")
    /// Master toggle for the two-tier "simple vs advanced" sidebar. When
    /// false, entries marked AdvancedOnly are filtered out of the QML tree
    /// accessors (topLevelPagesData / childPagesData) and any virtual
    /// category left with no visible descendant vanishes with them; when
    /// true, SimpleOnly entries are hidden instead. Defaults to true so an
    /// app that never marks a page keeps the classic "show everything"
    /// behaviour. The value is app UI state — the app drives it (persisted
    /// however the app likes); the registry only consumes it for filtering.
    Q_PROPERTY(bool showAdvanced READ showAdvanced WRITE setShowAdvanced NOTIFY showAdvancedChanged)

public:
    /// Per-entry visibility tier for the simple/advanced split. `Always`
    /// (the default) shows the page in both modes; `AdvancedOnly` hides it
    /// while `showAdvanced` is false; `SimpleOnly` hides it while
    /// `showAdvanced` is true (for purpose-built simplified pages that a
    /// richer advanced page supersedes). Filtering happens only in the
    /// QML-facing tree accessors — `pageData` / `allPagesData` /
    /// `controller` stay unfiltered so a currently-shown or dirty page
    /// always resolves regardless of mode.
    enum class PageVisibility {
        Always,
        AdvancedOnly,
        SimpleOnly,
    };
    Q_ENUM(PageVisibility)

    /**
     * Public-by-value view of a registry entry. Consumers receive copies
     * via topLevelPages() / childPages() / allPages() / entry(); they
     * may NOT mutate the underlying controller pointer to detach it
     * from the registry. The QPointer surface is read-only by
     * convention — the registry is the only writer.
     *
     * Out-of-tree code that needs to act on the controller should
     * prefer the explicit `controller(id)` accessor (which performs
     * the live-pointer null-check) over Entry.controller; the field
     * is exposed here only because Sidebar.qml drives Repeaters off
     * `topLevelPagesData()` / `childPagesData()` which serialize the
     * Entry's struct directly into QVariantMap.
     */
    struct Entry
    {
        QString id;
        QString parentId; // empty == top-level
        QString title; // already translated by caller
        QString iconSource; // freedesktop icon name or QML asset URL; optional
        QUrl qmlSource; // page QML file URL
        // QPointer guards against dangling lookups if a consumer destroys
        // the controller out-of-order relative to the registry. Symmetric
        // with ApplicationController::m_domains.
        QPointer<PageController> controller;
        /// When true, the Sidebar renders this entry as an inline-expandable
        /// category header rather than a drill-down target — its children
        /// appear indented under it (toggleable) instead of replacing the
        /// list. Useful for shallow grouping ("Display", "Rules", `*-cat`
        /// buckets) inside an otherwise drill-down sidebar.
        bool isCollapsible = false;
        /// When true, the Sidebar draws a horizontal divider line in the
        /// row slot immediately after this entry. Used to break up the
        /// vertical rhythm of long sidebars without grouping items under
        /// a category header. Suppressed while a search filter is active
        /// — dividers are navigation ornament, not match metadata.
        bool hasDividerAfter = false;
        /// Simple/advanced tier for this page. Stamped after registration
        /// via setPageVisibility(); defaults to Always so unclassified
        /// pages show in both modes. See PageVisibility.
        PageVisibility visibility = PageVisibility::Always;
    };

    explicit PageRegistry(QObject* parent = nullptr);
    ~PageRegistry() override;

    /** Register a page. Emits `pageRegistered(id)` and returns `true` on
     *  success. Warns and returns `false` on any of: empty id, duplicate
     *  id, unknown parentId, null controller. The intent is to surface
     *  programmer errors in the log without aborting startup; the
     *  PageRegistry's tree just won't contain the misconfigured
     *  entry — downstream Sidebar / Breadcrumbs / page-router lookups
     *  for it will return empty.
     *
     *  Callers that perform additional bookkeeping per registered page
     *  (e.g. ApplicationController tracking the page as a staging domain)
     *  must gate that bookkeeping on the returned bool — otherwise a
     *  rejected page leaks into half-registered state where downstream
     *  systems mutate it but the UI cannot see it. */
    bool registerPage(Entry entry);

    /// Stamp a page's simple/advanced tier after it has been registered.
    /// No-op (with a warning) for an unknown id. Kept separate from
    /// registerPage so the classification lives in one post-build pass in
    /// the app rather than threaded through every registration call site.
    /// Intended to run at startup before the first paint — it does not emit
    /// a change signal of its own (the initial showAdvanced push drives the
    /// first filtered build).
    void setPageVisibility(const QString& id, PageVisibility visibility);

    bool showAdvanced() const;
    void setShowAdvanced(bool showAdvanced);

    Q_INVOKABLE bool hasPage(const QString& id) const;
    Q_INVOKABLE PhosphorControl::PageController* controller(const QString& id) const;
    /** Look up an Entry by id. Returns a default-constructed (empty) Entry
     *  when the id is unknown — callers that need to distinguish "no such
     *  page" from "page exists with empty optional fields" should call
     *  hasPage() first. */
    Entry entry(const QString& id) const;

    QList<Entry> topLevelPages() const;
    QList<Entry> childPages(const QString& parentId) const;
    QList<Entry> allPages() const;

    // QML-facing accessors. Return dicts with keys: id, parentId, title,
    // iconSource, qmlSource. Used by Sidebar.qml + Breadcrumbs.qml to drive
    // their Repeaters without needing a custom QAbstractListModel.
    Q_INVOKABLE QVariantList topLevelPagesData() const;
    Q_INVOKABLE QVariantList childPagesData(const QString& parentId) const;
    Q_INVOKABLE QVariantMap pageData(const QString& id) const;
    /** Full flat list of every registered page, used by QML that needs
     *  to walk the whole catalogue (e.g. for an apply-on-close failure
     *  toast that names every page still dirty after applyAll()). */
    Q_INVOKABLE QVariantList allPagesData() const;

Q_SIGNALS:
    void pageRegistered(const QString& id);
    void showAdvancedChanged();

private:
    /// True iff `v` should be shown under the current `m_showAdvanced` mode
    /// (Always always; AdvancedOnly iff advanced; SimpleOnly iff simple).
    bool modeAllows(PageVisibility v) const;
    /// Full visibility test for a sidebar tree accessor: the entry's own
    /// tier must match the mode AND, for a virtual node (no qmlSource), at
    /// least one descendant must itself be visible — so an emptied-out
    /// category header disappears with its children. Recursion is bounded
    /// by the tree depth; the registry graph is acyclic by construction
    /// (registerPage rejects a parentId that isn't already registered).
    bool isEntryVisible(const Entry& entry) const;

    QList<Entry> m_pages;
    /// Simple/advanced master mode. Defaults true so an app that never
    /// classifies a page sees no behavioural change.
    bool m_showAdvanced = true;
    // qsizetype matches QList::size()'s return type; avoids -Wnarrowing
    // when stricter build flags land. Settings registries are never
    // anywhere near 2^31 pages, but the narrowing is unnecessary.
    QHash<QString, qsizetype> m_indexById;
    // Controller-pointer dedup set — registerPage previously did an
    // O(N) linear scan of m_pages per registration to reject a
    // duplicate controller, which compounded to O(K²) for K page
    // registrations. The set lookup keeps registration O(1) for
    // catalogues that grow to dozens of pages.
    QSet<PageController*> m_controllerSet;
};

} // namespace PhosphorControl
