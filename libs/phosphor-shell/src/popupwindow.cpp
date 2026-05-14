// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PopupWindow.h>

#include <QLoggingCategory>
#include <QQuickWindow>
#include <QtMath>

// Qt private headers for direct xdg_popup.reposition support — same
// pattern quickshell uses (src/wayland/popupanchor.cpp). The public
// _q_waylandPopup* properties are read once at xdg_popup creation
// time; changing them later doesn't trigger reposition. To switch a
// mapped popup's anchor/size without destroying and recreating the
// xdg_popup (which races the grab handoff and gets the new popup
// dismissed by the compositor), we have to call xdg_popup.reposition
// on the same proxy directly.
#include <qwaylandclientextension.h>
#include <private/qwayland-xdg-shell.h>
#include <private/qwaylandwindow_p.h>
#include <private/wayland-xdg-shell-client-protocol.h>

Q_LOGGING_CATEGORY(lcPopup, "phosphorshell.popup")

namespace {

// Singleton wrapper that binds xdg_wm_base from the Wayland registry
// via Qt's QWaylandClientExtensionTemplate. Quickshell uses the same
// "bind it ourselves rather than reach into Qt's private state"
// approach because Qt's xdg-shell module headers aren't exported.
//
// Bind version 6 — matches quickshell's pick. Reposition needs ≥3.
class XdgWmBaseExt
    : public QWaylandClientExtensionTemplate<XdgWmBaseExt>
    , public QtWayland::xdg_wm_base
{
public:
    static XdgWmBaseExt* instance()
    {
        static auto* inst = new XdgWmBaseExt();
        return inst;
    }

private:
    XdgWmBaseExt()
        : QWaylandClientExtensionTemplate(6)
    {
        initialize();
    }
};

} // namespace

namespace PhosphorShell {

PopupWindow::PopupWindow(QQuickItem* parent)
    : QQuickItem(parent)
    , m_contentItem(new QQuickItem(this))
{
    // m_contentItem is the persistent QML default-property host. Until
    // showPopup() materialises a QQuickWindow, m_contentItem lives as
    // a child of `this` (the PopupWindow QQuickItem) so the QML scope
    // chain works during component construction. On first show we
    // re-parent m_contentItem ONCE to the popup's QQuickWindow content
    // root and never move it again — children declared via the default
    // property remain attached to m_contentItem and their binding
    // contexts stay stable through any subsequent show/hide/reposition
    // cycles. The previous implementation moved every direct child to
    // the popup window's contentItem on first show, which severed the
    // property-change notification path for bindings reaching
    // back into the host's properties — manifesting as content not
    // updating when the host swapped state while the popup was mapped.
    m_contentItem->setObjectName(QStringLiteral("PopupWindowContent"));
}

PopupWindow::~PopupWindow()
{
    // Hide first so the compositor sees a clean unmap before the wl_surface
    // is destroyed by ~QQuickWindow.
    hidePopup();
    // unique_ptr destructor reclaims m_popupWindow.
}

// QQmlListProperty static dispatch functions for our `data` override.
// Forwarding via `m_contentItem->property("data").value<QQmlListProperty<QObject>>()`
// doesn't survive — QQmlListProperty's function-pointer table captures
// internal QQuickItem state that doesn't round-trip through QVariant
// copies, so QML's default-property installer ends up with a list
// whose append() is a no-op. Children land nowhere visible and the
// QML hierarchy fails to construct (manifesting as missing widgets).
//
// Instead we expose our OWN list property whose dispatch functions
// reach into m_contentItem and call its real `data` property at the
// QQuickItem level. The four functions (append/count/at/clear) are
// the standard QQmlListProperty contract.
static QQuickItem* contentItemFromData(QQmlListProperty<QObject>* prop)
{
    return static_cast<QQuickItem*>(prop->data);
}

static QQmlListProperty<QObject> contentDataList(QQuickItem* item)
{
    QVariant v = item->property("data");
    Q_ASSERT(v.canConvert<QQmlListProperty<QObject>>());
    return v.value<QQmlListProperty<QObject>>();
}

static void popupDataAppend(QQmlListProperty<QObject>* prop, QObject* obj)
{
    auto list = contentDataList(contentItemFromData(prop));
    if (list.append)
        list.append(&list, obj);
}

static qsizetype popupDataCount(QQmlListProperty<QObject>* prop)
{
    auto list = contentDataList(contentItemFromData(prop));
    return list.count ? list.count(&list) : 0;
}

static QObject* popupDataAt(QQmlListProperty<QObject>* prop, qsizetype i)
{
    auto list = contentDataList(contentItemFromData(prop));
    return list.at ? list.at(&list, i) : nullptr;
}

static void popupDataClear(QQmlListProperty<QObject>* prop)
{
    auto list = contentDataList(contentItemFromData(prop));
    if (list.clear)
        list.clear(&list);
}

QQmlListProperty<QObject> PopupWindow::data()
{
    // `data` property pointer is m_contentItem; the dispatch functions
    // above forward each operation to m_contentItem's real `data`
    // QQmlListProperty in-place. This avoids the QVariant round-trip
    // that loses the list's function-pointer table.
    return QQmlListProperty<QObject>(this, m_contentItem, &popupDataAppend, &popupDataCount, &popupDataAt,
                                     &popupDataClear);
}

QQuickItem* PopupWindow::anchor() const
{
    return m_anchor;
}

void PopupWindow::setAnchor(QQuickItem* anchor)
{
    if (m_anchor == anchor) {
        return;
    }
    m_anchor = anchor;
    Q_EMIT anchorChanged();
    // Re-issue the xdg-positioner. The positioner can only be set at
    // xdg-popup creation time, so a live anchor change requires hide+show.
    reapplyIfVisible();
}

int PopupWindow::popupWidth() const
{
    return m_popupWidth;
}

void PopupWindow::setPopupWidth(int width)
{
    // Clamp to >= 1 — Wayland surface protocols reject 0-dim surfaces,
    // and a negative width has no meaning.
    const int clamped = qMax(1, width);
    if (m_popupWidth == clamped) {
        return;
    }
    m_popupWidth = clamped;
    Q_EMIT popupWidthChanged();
    if (m_popupWindow) {
        m_popupWindow->setWidth(m_popupWidth);
    }
    m_contentItem->setWidth(m_popupWidth);
    // The xdg-positioner's set_size is part of the popup's initial
    // configuration; live size changes need a reposition (or hide+show
    // fallback). Without this, calling popupWidth = 300 on an already-
    // mapped popup just resizes the QML/RHI surface but leaves the
    // compositor positioning the popup with the original size.
    reapplyIfVisible();
}

int PopupWindow::popupHeight() const
{
    return m_popupHeight;
}

void PopupWindow::setPopupHeight(int height)
{
    const int clamped = qMax(1, height);
    if (m_popupHeight == clamped) {
        return;
    }
    m_popupHeight = clamped;
    Q_EMIT popupHeightChanged();
    if (m_popupWindow) {
        m_popupWindow->setHeight(m_popupHeight);
    }
    m_contentItem->setHeight(m_popupHeight);
    reapplyIfVisible();
}

PopupWindow::PopupEdge PopupWindow::popupEdge() const
{
    return m_popupEdge;
}

void PopupWindow::setPopupEdge(PopupEdge edge)
{
    if (m_popupEdge == edge) {
        return;
    }
    m_popupEdge = edge;
    Q_EMIT popupEdgeChanged();
    reapplyIfVisible();
}

int PopupWindow::gap() const
{
    return m_gap;
}

void PopupWindow::setGap(int gap)
{
    if (m_gap == gap) {
        return;
    }
    m_gap = gap;
    Q_EMIT gapChanged();
    reapplyIfVisible();
}

bool PopupWindow::isPopupVisible() const
{
    return m_popupVisible;
}

void PopupWindow::setPopupVisible(bool visible)
{
    if (m_popupVisible == visible) {
        return;
    }
    m_popupVisible = visible;

    if (visible) {
        showPopup();
    } else {
        hidePopup();
    }

    Q_EMIT popupVisibleChanged();
}

void PopupWindow::close()
{
    setPopupVisible(false);
}

QRect PopupWindow::computeAnchorRect() const
{
    if (!m_anchor) {
        return {};
    }

    const QPointF scenePos = m_anchor->mapToScene(QPointF(0, 0));
    // qCeil instead of static_cast<int> — truncation can produce a sub-
    // pixel anchor rect that misses the actual anchor's right/bottom edge,
    // which makes the xdg-positioner snap the popup against the wrong
    // edge for fractional-DPI items.
    return QRect(scenePos.toPoint(), QSize(qCeil(m_anchor->width()), qCeil(m_anchor->height())));
}

void PopupWindow::showPopup()
{
    if (!m_anchor || !m_anchor->window()) {
        qCWarning(lcPopup) << "Cannot show popup: no anchor or anchor has no window";
        return;
    }

    if (!m_popupWindow) {
        m_popupWindow = std::make_unique<QQuickWindow>();
        m_popupWindow->setFlag(Qt::Popup);
        m_popupWindow->setColor(Qt::transparent);

        // Move the persistent content host into the popup window. This
        // is the only re-parent ever performed — children declared in
        // QML are children of m_contentItem, never of `this`, so they
        // don't get touched.
        m_contentItem->setParentItem(m_popupWindow->contentItem());
    }

    m_popupWindow->setTransientParent(m_anchor->window());
    m_popupWindow->resize(m_popupWidth, m_popupHeight);
    // Keep the content host sized to the popup so anchors.fill on QML
    // children resolves to the popup's full client area.
    m_contentItem->setWidth(m_popupWidth);
    m_contentItem->setHeight(m_popupHeight);

    // Translate popupEdge → Wayland xdg-popup positioner anchor + gravity.
    //
    // Without these, Qt's QtWayland defaults to "no anchor / no gravity"
    // → popup ends up centered ON the anchor rect (overlapping it). The
    // bug was hidden for menus anchored to a small top-left button — the
    // default placement happened to land roughly below+right — but became
    // obvious when the anchor lived at the centre of a wider panel.
    //
    // The set of properties Qt's xdg-shell client plugin actually reads
    // (verified by `strings libxdg-shell.so`):
    //   _q_waylandPopupAnchorRect          (QRect)
    //   _q_waylandPopupAnchor              (Qt::Edges → xdg anchor)
    //   _q_waylandPopupGravity             (Qt::Edges → xdg gravity)
    //   _q_waylandPopupConstraintAdjustment (uint, bitmask)
    // There is NO _q_waylandPopupAnchorOffset — Qt does not honour an
    // offset property. We bake the gap into the anchor rect itself by
    // extending the rect past the anchor item's edge by m_gap pixels in
    // the direction we want the popup to attach to.
    Qt::Edges anchorEdges;
    Qt::Edges gravityEdges;
    QRect anchorRect = computeAnchorRect();
    switch (m_popupEdge) {
    case Above:
        anchorEdges = Qt::TopEdge;
        gravityEdges = Qt::TopEdge;
        anchorRect.adjust(0, -m_gap, 0, 0); // grow upward
        break;
    case Below:
        anchorEdges = Qt::BottomEdge;
        gravityEdges = Qt::BottomEdge;
        anchorRect.adjust(0, 0, 0, m_gap); // grow downward
        break;
    case LeftOf:
        anchorEdges = Qt::LeftEdge;
        gravityEdges = Qt::LeftEdge;
        anchorRect.adjust(-m_gap, 0, 0, 0); // grow leftward
        break;
    case RightOf:
        anchorEdges = Qt::RightEdge;
        gravityEdges = Qt::RightEdge;
        anchorRect.adjust(0, 0, m_gap, 0); // grow rightward
        break;
    }

    // xdg_positioner.set_anchor_rect requires the rect to lie within the
    // parent surface's logical bounds AND be non-empty; an out-of-bounds
    // or zero-area rect is a protocol error that drops the wl_display
    // connection. Clamp our gap-extended rect to the parent QQuickWindow's
    // content bounds (which equals the parent surface for layer-shell
    // parents). If the anchor item is fully off-surface and BOTH the
    // gap-extended and unextended rects collapse to empty, abort the
    // show — sending an empty positioner would kill the connection.
    if (auto* parentWindow = m_anchor->window()) {
        const QRect surfaceBounds(0, 0, parentWindow->width(), parentWindow->height());
        anchorRect = anchorRect.intersected(surfaceBounds);
        if (anchorRect.isEmpty()) {
            // Fall back to the un-extended anchor rect — better to lose
            // the gap than to send an invalid positioner.
            anchorRect = computeAnchorRect().intersected(surfaceBounds);
        }
        if (anchorRect.isEmpty()) {
            qCWarning(lcPopup) << "Cannot show popup: anchor item is off-surface (computed rect empty); "
                                  "skipping show to avoid xdg-positioner protocol error";
            // Roll the visibility flag back so a subsequent
            // setPopupVisible(true) re-attempts cleanly. We don't emit
            // popupVisibleChanged again — caller already saw `true`.
            m_popupVisible = false;
            return;
        }
    }

    m_popupWindow->setProperty("_q_waylandPopupAnchorRect", anchorRect);
    // Pass as uint — Qt's xdg-shell plugin reads these via QVariant::toUInt
    // and uses the value as a Qt::Edges-indexed jump-table key. Passing as
    // QFlags<Qt::Edge> via QVariant::fromValue can fail to convert (no
    // registered Qt::Edges→uint conversion) and the property silently
    // falls back to its default (top-left anchor / bottom-right gravity =
    // popup roughly right-of-and-below the anchor — which matches the
    // observed misplacement).
    m_popupWindow->setProperty("_q_waylandPopupAnchor", static_cast<uint>(anchorEdges));
    m_popupWindow->setProperty("_q_waylandPopupGravity", static_cast<uint>(gravityEdges));
    // 0xF = SlideX|SlideY|FlipX|FlipY — let the compositor reposition the
    // popup if it'd run off-screen.
    m_popupWindow->setProperty("_q_waylandPopupConstraintAdjustment", static_cast<uint>(0xF));

    qCDebug(lcPopup) << "Showing popup: anchorRect=" << anchorRect << "size=" << m_popupWidth << "x" << m_popupHeight
                     << "edge=" << m_popupEdge << "anchor=" << static_cast<uint>(anchorEdges)
                     << "gravity=" << static_cast<uint>(gravityEdges)
                     << "anchorItem.scenePos=" << (m_anchor ? m_anchor->mapToScene(QPointF(0, 0)) : QPointF())
                     << "anchorItem.size=" << (m_anchor ? QSizeF(m_anchor->width(), m_anchor->height()) : QSizeF());
    m_popupWindow->show();
}

void PopupWindow::hidePopup()
{
    if (m_popupWindow && m_popupWindow->isVisible()) {
        m_popupWindow->hide();
        qCDebug(lcPopup) << "Popup hidden";
    }
}

bool PopupWindow::repositionInPlace()
{
    if (!m_popupVisible || !m_popupWindow || !m_anchor || !m_anchor->window()) {
        return false;
    }
    // Walk Qt's xdg-shell client to get the live xdg_popup proxy. Pattern
    // lifted from quickshell/src/wayland/popupanchor.cpp:20.
    auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(m_popupWindow->handle());
    if (!waylandWindow)
        return false;
    auto* popupRole = waylandWindow->surfaceRole<::xdg_popup>();
    if (!popupRole)
        return false;

    auto* xdgWmBase = XdgWmBaseExt::instance();
    if (!xdgWmBase->isInitialized()) {
        qCDebug(lcPopup) << "xdg_wm_base not bound — cannot reposition; falling back to hide+show";
        return false;
    }
    if (xdgWmBase->QtWayland::xdg_wm_base::version() < XDG_POPUP_REPOSITION_SINCE_VERSION) {
        qCDebug(lcPopup) << "xdg_wm_base version < 3 — reposition unsupported, falling back to hide+show";
        return false;
    }

    // Compute the anchor rect + edge/gravity flags exactly as showPopup does.
    QRect anchorRect = computeAnchorRect();
    QtWayland::xdg_positioner::anchor anchorFlag = QtWayland::xdg_positioner::anchor_none;
    QtWayland::xdg_positioner::gravity gravityFlag = QtWayland::xdg_positioner::gravity_none;
    switch (m_popupEdge) {
    case Above:
        anchorFlag = QtWayland::xdg_positioner::anchor_top;
        gravityFlag = QtWayland::xdg_positioner::gravity_top;
        anchorRect.adjust(0, -m_gap, 0, 0);
        break;
    case Below:
        anchorFlag = QtWayland::xdg_positioner::anchor_bottom;
        gravityFlag = QtWayland::xdg_positioner::gravity_bottom;
        anchorRect.adjust(0, 0, 0, m_gap);
        break;
    case LeftOf:
        anchorFlag = QtWayland::xdg_positioner::anchor_left;
        gravityFlag = QtWayland::xdg_positioner::gravity_left;
        anchorRect.adjust(-m_gap, 0, 0, 0);
        break;
    case RightOf:
        anchorFlag = QtWayland::xdg_positioner::anchor_right;
        gravityFlag = QtWayland::xdg_positioner::gravity_right;
        anchorRect.adjust(0, 0, m_gap, 0);
        break;
    }
    // Same surface-bounds clamp as showPopup — empty rect would crash
    // the connection.
    if (auto* parentWindow = m_anchor->window()) {
        const QRect surfaceBounds(0, 0, parentWindow->width(), parentWindow->height());
        anchorRect = anchorRect.intersected(surfaceBounds);
        if (anchorRect.isEmpty())
            anchorRect = computeAnchorRect().intersected(surfaceBounds);
        if (anchorRect.isEmpty()) {
            qCWarning(lcPopup) << "repositionInPlace: anchor rect empty after clamp — skipping";
            return false;
        }
    }

    // Build a fresh positioner. xdg_positioner is a one-shot resource:
    // create, configure, hand to reposition, destroy.
    auto positioner = QtWayland::xdg_positioner(xdgWmBase->create_positioner());
    positioner.set_anchor_rect(anchorRect.x(), anchorRect.y(), anchorRect.width(), anchorRect.height());
    positioner.set_anchor(anchorFlag);
    positioner.set_gravity(gravityFlag);
    positioner.set_size(m_popupWidth, m_popupHeight);
    positioner.set_constraint_adjustment(0xF); // SlideX|SlideY|FlipX|FlipY

    // The token is echoed back via xdg_popup.repositioned for clients that
    // want to correlate request-with-configure. We don't, so 0 is fine.
    xdg_popup_reposition(popupRole, positioner.object(), 0);
    positioner.destroy();

    // Resize the QQuickWindow so Qt's RHI surface matches the new popup
    // size — the compositor will issue xdg_popup.configure with the
    // constrained size eventually, but locally we want geometry to be
    // right for the initial paint.
    m_popupWindow->resize(m_popupWidth, m_popupHeight);

    qCDebug(lcPopup) << "Repositioned popup in-place: rect=" << anchorRect << "size=" << m_popupWidth << "x"
                     << m_popupHeight << "edge=" << m_popupEdge;
    return true;
}

void PopupWindow::reapplyIfVisible()
{
    if (!m_popupVisible || !m_popupWindow) {
        return;
    }
    // Try the in-place reposition path first — preserves the xdg_popup
    // grab so the popup stays mapped across the transition. This is the
    // path that makes anchor/size switching between sibling popups work
    // without flashing or chaining (the xdg-shell grab handoff between
    // sibling popups under the same parent is forbidden by the spec).
    if (repositionInPlace())
        return;

    // Fallback for compositors that don't support xdg_popup.reposition
    // (xdg_wm_base < v3): hide+show cycle re-creates the xdg-popup with
    // a fresh positioner. The QQuickWindow itself is reused (children
    // stay parented to its contentItem); only the wl_surface role is
    // replayed.
    //
    // Capture the focused item before hiding so we can restore focus
    // after the show — without this a TextField / SpinBox / Button that
    // had keyboard focus would lose it whenever the popup re-applies
    // its positioner (e.g. anchor moves while the popup is open).
    QPointer<QQuickItem> focusedBefore(m_popupWindow->activeFocusItem());
    hidePopup();
    showPopup();
    // Only restore focus if showPopup actually succeeded — the off-
    // surface guard in showPopup rolls m_popupVisible back to false
    // and returns without making the popup visible. forceActiveFocus
    // on an item under a hidden window would just queue focus on a
    // surface the user can't see.
    if (m_popupVisible && focusedBefore) {
        focusedBefore->forceActiveFocus();
    }
}

} // namespace PhosphorShell
