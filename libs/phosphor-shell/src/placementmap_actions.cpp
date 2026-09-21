// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// PlacementMapScreen's verbs (A2 §1.5): everything the miniature can ask
// the engine to do, plus the drop proxy and the right-click menu. Every
// action routes through the daemon so it behaves exactly as the keyboard
// verbs do; nothing here bypasses the mode router. Phase-3 surfaces are
// probed once and latched off on UnknownMethod, like phase 2.

#include <PhosphorShell/PlacementMap.h>

#include "placementmap_p.h"

#include <PhosphorShell/Workspaces.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

namespace {
// getLayoutPreviewList / getScrollingTemplates keys, pinned beside the
// only reader (the XML docstrings are the contract).
constexpr QLatin1String PreviewId("id");
constexpr QLatin1String PreviewDisplayName("displayName");
constexpr QLatin1String PreviewIsAutotile("isAutotile");
constexpr QLatin1String PreviewIsScrollingTemplate("isScrollingTemplate");
constexpr QLatin1String TemplateId("id");
constexpr QLatin1String TemplateName("name");

// menuModel entry keys and kinds.
constexpr QLatin1String KeyKind("kind");
constexpr QLatin1String KeyId("id");
constexpr QLatin1String KeyName("name");
constexpr QLatin1String KeyCurrent("current");
constexpr QLatin1String KindLayout("layout");
constexpr QLatin1String KindAlgorithm("algorithm");
constexpr QLatin1String KindTemplate("template");
constexpr QLatin1String KindVerb("verb");

// Verb ids, labelled by the QML that shows them.
constexpr QLatin1String VerbEditLayout("editLayout");
constexpr QLatin1String VerbSnapAll("snapAll");
constexpr QLatin1String VerbRetile("retile");
constexpr QLatin1String VerbPromoteToMaster("promoteToMaster");
constexpr QLatin1String VerbToggleMaximizeColumn("toggleMaximizeColumn");

QVariantMap choice(QLatin1String kind, const QString& id, const QString& name, bool current)
{
    QVariantMap m;
    m.insert(KeyKind, kind);
    m.insert(KeyId, id);
    m.insert(KeyName, name);
    m.insert(KeyCurrent, current);
    return m;
}

QVariantMap verb(QLatin1String id)
{
    QVariantMap m;
    m.insert(KeyKind, KindVerb);
    m.insert(KeyId, id);
    return m;
}
} // namespace

namespace PhosphorShell {

using namespace PlacementMapParser;
using namespace PlacementMapIface;

// ─── Click, wheel, desktops ───────────────────────────────────────────

void PlacementMapScreen::activate(const QString& id)
{
    const Cell* cell = cellById(id);
    if (!cell || !m_map->isAvailable()) {
        return;
    }
    switch (m_mode) {
    case Snapping:
        if (cell->occupied) {
            activateWindow(cell->windowId);
            return;
        }
        if (cell->zoneNumber > 0) {
            m_map->m_bus->call(Iface::Snap, QStringLiteral("snapToZoneByNumber"), {cell->zoneNumber, m_screenId});
        }
        break;
    case Tiling:
        activateWindow(cell->windowId);
        break;
    case Scrolling:
        if (cell->focused) {
            return;
        }
        if (cell->columnIndex >= 0 && m_map->m_caps.focusColumnAt) {
            // The stepping fallback needs the cell after the reply, and
            // the resolved list may have been rebuilt by then, so it is
            // re-found by id.
            const QString cellId = cell->id;
            call<void>(
                Iface::Scrolling, QStringLiteral("focusColumnAt"), {m_screenId, cell->columnIndex}, [] { },
                [this, cellId](const QDBusError& error) {
                    if (latchUnknownMethod(m_map->m_caps.focusColumnAt, error)) {
                        stepFocusToward(cellById(cellId));
                    }
                });
            return;
        }
        stepFocusToward(cell);
        break;
    default:
        break;
    }
}

void PlacementMapScreen::activateWindow(const QString& windowId)
{
    // Without the verb (older daemon) a click on an occupied cell does
    // nothing, as in phase 2; there is no focus-by-id surface to fall
    // back on.
    if (windowId.isEmpty() || !m_map->m_caps.activateWindow) {
        return;
    }
    call<void>(
        Iface::WindowTracking, QStringLiteral("activateWindow"), {windowId}, [] { },
        [this](const QDBusError& error) {
            latchUnknownMethod(m_map->m_caps.activateWindow, error);
        });
}

void PlacementMapScreen::stepFocusToward(const Cell* cell)
{
    // Older daemon: only ±1 focus steps exist, so walk from the focused
    // column to the clicked one.
    if (!cell) {
        return;
    }
    int from = -1;
    int to = -1;
    for (int i = 0; i < m_resolved.size(); ++i) {
        if (m_resolved[i].focused) {
            from = i;
        }
        if (&m_resolved[i] == cell) {
            to = i;
        }
    }
    if (from < 0 || to < 0 || from == to) {
        return;
    }
    const int step = to > from ? 1 : -1;
    for (int i = from; i != to; i += step) {
        m_map->m_bus->call(Iface::Scrolling, QStringLiteral("focusColumn"), {m_screenId, step});
    }
}

void PlacementMapScreen::scrollViewByPx(int px)
{
    if (!m_map->isAvailable() || m_mode != Scrolling || px == 0 || !m_map->m_caps.scrollViewByPx) {
        return;
    }
    // No pixel-precise pan exists on an older daemon; the lens drag is
    // simply inert there rather than approximated with whole-column steps.
    call<void>(
        Iface::Scrolling, QStringLiteral("scrollViewByPx"), {m_screenId, px}, [] { },
        [this](const QDBusError& error) {
            latchUnknownMethod(m_map->m_caps.scrollViewByPx, error);
        });
}

void PlacementMapScreen::scrollView(int delta)
{
    if (!m_map->isAvailable() || delta == 0) {
        return;
    }
    const int step = delta > 0 ? 1 : -1;
    switch (m_mode) {
    case Scrolling:
        m_map->m_bus->call(Iface::Scrolling, QStringLiteral("scrollView"), {m_screenId, step});
        break;
    case Snapping:
        m_map->m_bus->call(Iface::Snap, QStringLiteral("focusAdjacentZone"),
                           {step > 0 ? QStringLiteral("right") : QStringLiteral("left")});
        break;
    case Tiling:
        m_map->m_bus->call(Iface::Autotile, step > 0 ? QStringLiteral("focusNext") : QStringLiteral("focusPrevious"),
                           {});
        break;
    default:
        break;
    }
}

void PlacementMapScreen::switchDesktop(int index)
{
    Workspaces* ws = m_map->workspaces();
    if (!ws || index < 0) {
        return;
    }
    const QModelIndex mi = ws->model()->index(index, 0);
    if (!mi.isValid()) {
        return;
    }
    ws->switchTo(ws->model()->data(mi, WorkspaceListModel::IdRole).toString());
}

int PlacementMapScreen::wireDesktop() const
{
    // A pinned screen IS its desktop.
    if (isPinned()) {
        return m_pinnedDesktop + 1;
    }
    // The daemon's own resolved desktop for this screen (getScreenStates)
    // is the context an assignment must land on; the workspace row is the
    // fallback before the states have answered.
    if (m_state.virtualDesktop > 0) {
        return m_state.virtualDesktop;
    }
    return m_currentDesktop >= 0 ? m_currentDesktop + 1 : 0;
}

// ─── Drag, float, desktop move ────────────────────────────────────────

void PlacementMapScreen::moveCell(const QString& fromId, const QString& toId)
{
    const Cell* from = cellById(fromId);
    const Cell* to = cellById(toId);
    if (!from || !to || from == to || !m_map->isAvailable()) {
        return;
    }
    switch (m_mode) {
    case Snapping:
        if (!from->occupied || from->windowId.isEmpty()) {
            return;
        }
        if (to->occupied && !to->windowId.isEmpty()) {
            m_map->m_bus->call(Iface::Snap, QStringLiteral("swapWindowsById"), {from->windowId, to->windowId});
        } else {
            m_map->m_bus->call(Iface::Snap, QStringLiteral("moveWindowToZone"), {from->windowId, to->id});
        }
        break;
    case Tiling:
        m_map->m_bus->call(Iface::Autotile, QStringLiteral("swapWindows"), {from->id, to->id});
        break;
    case Scrolling:
        // No column reorder exists on an older daemon; the drag is inert
        // there rather than approximated.
        if (from->columnIndex < 0 || to->columnIndex < 0 || !m_map->m_caps.moveColumnTo) {
            return;
        }
        call<void>(
            Iface::Scrolling, QStringLiteral("moveColumnTo"), {m_screenId, from->columnIndex, to->columnIndex}, [] { },
            [this](const QDBusError& error) {
                latchUnknownMethod(m_map->m_caps.moveColumnTo, error);
            });
        break;
    default:
        break;
    }
}

void PlacementMapScreen::toggleFloat(const QString& id)
{
    const Cell* cell = cellById(id);
    if (!cell || !cell->occupied || cell->windowId.isEmpty() || !m_map->isAvailable()) {
        return;
    }
    if (m_mode == Snapping) {
        m_map->m_bus->call(Iface::Snap, QStringLiteral("toggleFloatForWindow"), {cell->windowId, m_screenId});
        return;
    }
    if (m_mode == Tiling || m_mode == Scrolling) {
        setWindowFloating(cell->windowId);
    }
}

void PlacementMapScreen::setWindowFloating(const QString& windowId)
{
    // A tiled cell is non-floating by definition (floating tiles are not
    // drawn), so the toggle always floats. Without the per-screen setter
    // the Snap verb toggles the same per-mode slot.
    if (!m_map->m_caps.setWindowFloatingForScreen) {
        m_map->m_bus->call(Iface::Snap, QStringLiteral("toggleFloatForWindow"), {windowId, m_screenId});
        return;
    }
    call<void>(
        Iface::WindowTracking, QStringLiteral("setWindowFloatingForScreen"), {windowId, m_screenId, true}, [] { },
        [this, windowId](const QDBusError& error) {
            if (latchUnknownMethod(m_map->m_caps.setWindowFloatingForScreen, error)) {
                m_map->m_bus->call(Iface::Snap, QStringLiteral("toggleFloatForWindow"), {windowId, m_screenId});
            }
        });
}

void PlacementMapScreen::moveToDesktop(const QString& id, int index)
{
    const Cell* cell = cellById(id);
    if (!cell || cell->windowId.isEmpty() || index < 0 || index >= m_desktopCount || !m_map->isAvailable()
        || !m_map->m_caps.moveWindowToDesktop) {
        return;
    }
    // 0-based row in, 1-based desktop on the wire. Inert on an older
    // daemon (windowDesktopMoveRequested is signal-only there).
    call<void>(
        Iface::WindowTracking, QStringLiteral("moveWindowToDesktop"), {cell->windowId, index + 1}, [] { },
        [this](const QDBusError& error) {
            latchUnknownMethod(m_map->m_caps.moveWindowToDesktop, error);
        });
}

// ─── Drop proxy ───────────────────────────────────────────────────────

void PlacementMapScreen::registerDropProxy(const QRect& miniatureScreenRect, const QVariantList& cellScreenRects)
{
    // Snapping only: the daemon accepts and ignores a proxy elsewhere, so
    // there is no point sending one. An empty miniature is withdrawn.
    if (m_mode != Snapping || !m_map->isAvailable() || !m_map->m_caps.dropProxy || m_screenId.isEmpty()) {
        return;
    }
    if (miniatureScreenRect.isEmpty()) {
        unregisterDropProxy();
        return;
    }
    const QString json = dropProxyJson(miniatureScreenRect, cellScreenRects);
    if (json == m_dropProxyJson) {
        return;
    }
    sendDropProxy(json);
}

void PlacementMapScreen::sendDropProxy(const QString& json)
{
    m_dropProxyJson = json;
    call<void>(
        Iface::WindowDrag, QStringLiteral("registerDropProxy"), {m_screenId, json}, [] { },
        [this](const QDBusError& error) {
            if (latchUnknownMethod(m_map->m_caps.dropProxy, error)) {
                m_dropProxyJson.clear();
            }
        });
}

void PlacementMapScreen::unregisterDropProxy()
{
    if (m_dropProxyJson.isEmpty()) {
        return;
    }
    m_dropProxyJson.clear();
    if (m_map->isAvailable() && m_map->m_caps.dropProxy && !m_screenId.isEmpty()) {
        m_map->m_bus->call(Iface::WindowDrag, QStringLiteral("unregisterDropProxy"), {m_screenId});
    }
}

// ─── Right-click menu ─────────────────────────────────────────────────

void PlacementMapScreen::setMenu(const QVariantList& menu)
{
    if (menu == m_menu) {
        return;
    }
    m_menu = menu;
    Q_EMIT menuModelChanged();
}

void PlacementMapScreen::refreshMenu()
{
    ++m_menuGeneration;
    if (!m_map->isAvailable() || m_screenId.isEmpty()) {
        setMenu({});
        return;
    }
    switch (m_mode) {
    case Snapping:
        fetchSnappingMenu();
        break;
    case Tiling:
        fetchTilingMenu();
        break;
    case Scrolling:
        fetchScrollingMenu();
        break;
    default:
        setMenu({});
        break;
    }
}

void PlacementMapScreen::fetchSnappingMenu()
{
    const int generation = m_menuGeneration;
    call<QString>(
        Iface::LayoutRegistry, QStringLiteral("getLayoutPreviewList"), {}, [this, generation](const QString& json) {
            if (generation != m_menuGeneration) {
                return;
            }
            QVariantList menu;
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            const QJsonArray previews = doc.array();
            for (const QJsonValue& value : previews) {
                const QJsonObject preview = value.toObject();
                // The list is source-agnostic; only manual zone layouts are
                // snapping choices.
                if (preview[PreviewIsAutotile].toBool(false) || preview[PreviewIsScrollingTemplate].toBool(false)) {
                    continue;
                }
                const QString id = preview[PreviewId].toString();
                if (id.isEmpty()) {
                    continue;
                }
                menu.append(choice(KindLayout, id, preview[PreviewDisplayName].toString(), id == m_state.layoutId));
            }
            menu.append(verb(VerbEditLayout));
            menu.append(verb(VerbSnapAll));
            setMenu(menu);
        });
}

void PlacementMapScreen::fetchTilingMenu()
{
    const int generation = m_menuGeneration;
    call<QStringList>(
        Iface::Autotile, QStringLiteral("availableAlgorithms"), {}, [this, generation](const QStringList& ids) {
            if (generation != m_menuGeneration) {
                return;
            }
            // One algorithmInfo per id, gathered in list order and
            // published once the last has answered.
            struct Gather
            {
                QStringList ids;
                QHash<QString, QString> names;
                int pending = 0;
            };
            auto gather = std::make_shared<Gather>();
            gather->ids = ids;
            gather->ids.removeAll(QString());
            gather->pending = gather->ids.size();
            const auto publish = [this, gather] {
                QVariantList menu;
                for (const QString& id : std::as_const(gather->ids)) {
                    menu.append(choice(KindAlgorithm, id, gather->names.value(id, id), id == m_state.algorithmId));
                }
                menu.append(verb(VerbRetile));
                menu.append(verb(VerbPromoteToMaster));
                setMenu(menu);
            };
            if (gather->pending == 0) {
                publish();
                return;
            }
            for (const QString& id : std::as_const(gather->ids)) {
                const auto done = [this, generation, gather, publish] {
                    if (--gather->pending == 0 && generation == m_menuGeneration) {
                        publish();
                    }
                };
                call<PhosphorProtocol::AlgorithmInfoEntry>(
                    Iface::Autotile, QStringLiteral("algorithmInfo"), {id},
                    [gather, id, done](const PhosphorProtocol::AlgorithmInfoEntry& info) {
                        if (!info.name.isEmpty()) {
                            gather->names.insert(id, info.name);
                        }
                        done();
                    },
                    [done](const QDBusError&) {
                        done();
                    });
            }
        });
}

void PlacementMapScreen::fetchScrollingMenu()
{
    const int generation = m_menuGeneration;
    call<QString>(
        Iface::LayoutRegistry, QStringLiteral("getScrollingTemplates"), {}, [this, generation](const QString& json) {
            if (generation != m_menuGeneration) {
                return;
            }
            QVariantList menu;
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            const QJsonArray templates = doc.array();
            for (const QJsonValue& value : templates) {
                const QJsonObject t = value.toObject();
                const QString id = t[TemplateId].toString();
                if (id.isEmpty()) {
                    continue;
                }
                menu.append(choice(KindTemplate, id, t[TemplateName].toString(), id == m_state.scrollingTemplateId));
            }
            menu.append(verb(VerbToggleMaximizeColumn));
            setMenu(menu);
        });
}

void PlacementMapScreen::applyMenuChoice(const QString& kind, const QString& id)
{
    if (!m_map->isAvailable() || m_screenId.isEmpty() || id.isEmpty()) {
        return;
    }
    if (kind == KindVerb) {
        runVerb(id);
        return;
    }
    const int desktop = wireDesktop();
    if (desktop <= 0) {
        return;
    }
    // Each assignment verb stages the screen; applyAssignmentChanges is
    // the documented close that resnaps and announces it.
    if (kind == KindLayout) {
        m_map->m_bus->call(Iface::LayoutRegistry, QStringLiteral("assignLayoutToScreenDesktop"),
                           {m_screenId, desktop, id});
    } else if (kind == KindAlgorithm) {
        // Mode 1 (tiling). The snapping layout is left to inherit the
        // default; the stored scrolling template survives the call.
        m_map->m_bus->call(Iface::LayoutRegistry, QStringLiteral("setAssignmentEntry"),
                           {m_screenId, desktop, m_map->currentActivity(), int(Tiling), QString(), id});
    } else if (kind == KindTemplate) {
        m_map->m_bus->call(Iface::LayoutRegistry, QStringLiteral("setScrollingTemplateLayout"),
                           {m_screenId, desktop, m_map->currentActivity(), id});
    } else {
        return;
    }
    m_map->m_bus->call(Iface::LayoutRegistry, QStringLiteral("applyAssignmentChanges"), {});
}

void PlacementMapScreen::runVerb(const QString& id)
{
    const QString focused = effectiveFocusedWindowId();
    if (id == VerbEditLayout) {
        if (!m_state.layoutId.isEmpty()) {
            m_map->m_bus->call(Iface::LayoutRegistry, QStringLiteral("openEditorForLayoutOnScreen"),
                               {m_state.layoutId, m_screenId});
        }
    } else if (id == VerbSnapAll) {
        m_map->m_bus->call(Iface::Snap, QStringLiteral("snapAllWindows"), {m_screenId});
    } else if (id == VerbRetile) {
        m_map->m_bus->call(Iface::Tiling, QStringLiteral("retile"), {m_screenId});
    } else if (id == VerbPromoteToMaster) {
        if (!focused.isEmpty()) {
            m_map->m_bus->call(Iface::Autotile, QStringLiteral("promoteToMaster"), {focused});
        }
    } else if (id == VerbToggleMaximizeColumn) {
        if (!focused.isEmpty()) {
            m_map->m_bus->call(Iface::Scrolling, QStringLiteral("toggleMaximizeColumn"), {m_screenId, focused});
        }
    }
}

} // namespace PhosphorShell
