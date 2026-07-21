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
 * out-of-order. The catalogue only ever GROWS: entries are never removed
 * at runtime, but dynamic page registration (post-startup `registerPage`
 * calls) is supported and fires `pageRegistered`, and an entry's
 * simple/advanced tier can be restamped via setPageVisibility. Dynamic
 * removal is intentionally NOT supported — apps that need plugin-style
 * hot-unload should rebuild the controller.
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

    /// Depth bound for upward parent walks, shared by this class and
    /// ApplicationController::parentChainFor. registerPage rejects an entry
    /// whose parent is not already registered, so the graph is a DAG and a
    /// walk always terminates — this only bounds pathological nesting (real
    /// sidebars nest 3-4 deep). Declared here rather than duplicated per
    /// translation unit: the two walks have to agree, and a comment asking a
    /// human to keep two constants in step is not a mechanism.
    static constexpr int MaxParentChainHops = 32;

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
        /// Simple/advanced tier for this page. Declared at registration
        /// (set it on the Entry passed to registerPage); defaults to
        /// Always so unclassified pages show in both modes. See
        /// PageVisibility. setPageVisibility() remains for late
        /// reclassification but registration-time declaration is the
        /// canonical path — it has NO in-tree caller and is retained for
        /// plugin-style registration, where a host registers a page before
        /// it knows the tier. Do not delete it as dead code: its
        /// visibleSetChanged emit is the only reason that signal exists
        /// separately from showAdvancedChanged.
        PageVisibility visibility = PageVisibility::Always;
        /// Optional id of this page's other-mode counterpart. When a mode
        /// flip (or a deep link) hides this page, the app's navigation
        /// gate should land on the counterpart instead of a generic
        /// fallback — e.g. a purpose-built SimpleOnly page and the
        /// AdvancedOnly page it condenses point at each other. Empty
        /// means "no counterpart; use the app fallback". The registry
        /// stores the mapping; it does not act on it.
        /// Braced default like every other member here, so aggregate-init
        /// sites that omit the trailing field don't trip
        /// -Wmissing-field-initializers.
        QString counterpartId{};
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
    /// No-op (with a warning) for an unknown id, and a no-op when the tier is
    /// already what you are setting. Kept separate from registerPage so the
    /// classification lives in one post-build pass in the app rather than
    /// threaded through every registration call site. Normally runs at startup
    /// before the first paint, but a real change emits visibleSetChanged() so
    /// consumers that cache a tier-filtered view stay correct if it is called
    /// later. It does NOT emit showAdvancedChanged: the mode itself is
    /// untouched, only this one entry's tier.
    void setPageVisibility(const QString& id, PageVisibility visibility);

    bool showAdvanced() const;
    void setShowAdvanced(bool showAdvanced);

    Q_INVOKABLE bool hasPage(const QString& id) const;
    /** True iff the page is reachable under the current showAdvanced mode:
     *  its own visibility tier passes AND so does every ancestor's. The
     *  ancestor walk matters because hiding a category hides its whole
     *  subtree in the rail, so a page whose own tier passes under a filtered
     *  parent has no row and no drill path — answering "yes" for it would
     *  send search, keyboard next/prev and the mode gate somewhere the user
     *  cannot navigate back from. Unknown ids return true: this is a tier
     *  filter, not an existence check; validate with hasPage() first. It
     *  does NOT apply the empty-category descendant rule the tree accessors
     *  use — that asks "is this category worth drawing", not "may the user
     *  navigate here".
     *
     *  Reports FALSE for a page whose ancestry could not be resolved within
     *  MaxParentChainHops, and logs why (once per id). So a false answer means
     *  "hidden, or unverifiable" rather than strictly "hidden by tier" — the
     *  safe direction for every consumer, all of which treat false as skip. */
    bool pageAllowedInCurrentMode(const QString& id) const;
    /** Log a warning for every entry whose `counterpartId` names a page that
     *  does not exist, is itself, sits in the SAME tier (so a mode flip would
     *  redirect to something equally hidden), does not name this entry back
     *  (a one-way declaration dead-ends the RETURN flip on the app fallback),
     *  is not navigable (a category with no QML of its own — the redirect would
     *  land on an empty page body), or is itself unreachable in the mode that
     *  hides this entry (an ancestor category filters it out, so the redirect
     *  falls back anyway).
     *  Counterparts are stored unvalidated at registration because the target
     *  may be registered later, and nothing else ever checks them: a typo
     *  silently degrades every affected mode flip and deep link to the
     *  fallback page, and looks exactly like correct operation. Call once
     *  after registration completes. Returns true when every counterpart
     *  resolves. */
    bool validateCounterparts() const;
    /** Depth-first search (registration order) below `parentId` for the
     *  first navigable page visible under the current mode; empty string
     *  when nothing below the parent survives filtering. Pass an empty
     *  parentId to search from the root. This is the mode-aware
     *  replacement for a static "parent redirects to its first leaf"
     *  table. */
    /// Q_INVOKABLE: Breadcrumbs.qml routes a non-navigable ancestor crumb
    /// through this to avoid navigating into an empty page body.
    Q_INVOKABLE QString firstVisibleLeafId(const QString& parentId) const;
    Q_INVOKABLE PhosphorControl::PageController* controller(const QString& id) const;
    /** Look up an Entry by id. Returns a default-constructed (empty) Entry
     *  when the id is unknown — callers that need to distinguish "no such
     *  page" from "page exists with empty optional fields" should call
     *  hasPage() first. */
    Entry entry(const QString& id) const;

    QList<Entry> topLevelPages() const;
    QList<Entry> childPages(const QString& parentId) const;
    /** Children of @p parentId that survive the current mode's filter,
     *  including the empty-category rule (a virtual node with no surviving
     *  descendant drops out). The Entry-returning twin of childPagesData();
     *  childPages() above is deliberately UNFILTERED, so anything building a
     *  navigable view wants this one. */
    QList<Entry> visibleChildPages(const QString& parentId) const;
    QList<Entry> allPages() const;

    // QML-facing accessors. Return dicts whose canonical key set lives in
    // entryToVariant() (pageregistry.cpp): id, parentId, title, iconSource,
    // qmlSource, isCollapsible, hasDividerAfter, hasQmlSource. Used by
    // Sidebar.qml + Breadcrumbs.qml to drive their Repeaters without
    // needing a custom QAbstractListModel. `visibility` and `counterpartId`
    // are deliberately C++-only: tier filtering is applied here, inside the
    // tree accessors, so QML never needs to re-derive it, and the
    // counterpart is navigation policy the app owns.
    Q_INVOKABLE QVariantList topLevelPagesData() const;
    Q_INVOKABLE QVariantList childPagesData(const QString& parentId) const;
    Q_INVOKABLE QVariantMap pageData(const QString& id) const;
    /** Full flat list of every registered page, used by QML that needs
     *  to walk the whole catalogue (e.g. for an apply-on-close failure
     *  toast that names every page still dirty after applyAll()). */
    Q_INVOKABLE QVariantList allPagesData() const;

    /// Key under which the page-data dicts above carry the title. Exposed so
    /// SidebarRows::flatPageData can override that field through the SAME
    /// constant the registry writes it with, rather than its own independent
    /// "title" literal — two spellings of one schema key means a rename here
    /// would silently ADD a second key over there instead of overriding, and
    /// the assertion on the literal would stay green while the UI read the
    /// un-overridden value.
    static QLatin1String titleKey();

Q_SIGNALS:
    void pageRegistered(const QString& id);
    void showAdvancedChanged();
    /// The set of entries visible under the current mode may have changed —
    /// either the mode flipped or an entry was restamped. Consumers that
    /// cache a tier-filtered view (SearchController's index, any external
    /// tree mirror) should rebuild on this rather than on
    /// showAdvancedChanged, which fires only for a genuine mode flip.
    /// Kept distinct because showAdvancedChanged is the NOTIFY of the
    /// showAdvanced Q_PROPERTY: firing it when the property has not changed
    /// would re-evaluate every binding on it and mislead any consumer that
    /// reasonably reads it as "the user switched modes".
    void visibleSetChanged();

private:
    /// True iff `v` should be shown under the current `m_showAdvanced` mode
    /// (Always always; AdvancedOnly iff advanced; SimpleOnly iff simple).
    bool modeAllows(PageVisibility v) const;
    /// modeAllows against an EXPLICIT mode rather than the current one, so
    /// validateCounterparts can ask "would this be reachable in the mode that
    /// hides its partner" without mutating m_showAdvanced.
    static bool modeAllowsIn(PageVisibility v, bool advanced);
    /// pageAllowedInCurrentMode against an explicit mode. Same ancestor walk,
    /// same fail-closed behaviour; the public accessor binds `advanced` to
    /// m_showAdvanced.
    bool allowedInMode(const QString& id, bool advanced) const;
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
    /// Ids already warned about for an unresolvable ancestor chain. The check
    /// runs per entry per search-index rebuild and per keyboard/history step,
    /// so the diagnostic is once-per-id rather than once-per-call. Mutable
    /// because the reachability query is const.
    mutable QSet<QString> m_depthWarned;
};

} // namespace PhosphorControl
