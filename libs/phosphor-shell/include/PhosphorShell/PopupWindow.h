// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QPointer>
#include <QQmlListProperty>
#include <QQuickItem>
#include <QRect>

#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT PopupWindow : public QQuickItem
{
    Q_OBJECT

    // QML default property — children declared inside PopupWindow {}
    // land on the persistent m_contentItem rather than on PopupWindow
    // itself. This matters because m_contentItem is the only thing that
    // ever gets re-parented to the popup's QQuickWindow contentItem;
    // declared children stay rooted in m_contentItem from creation,
    // which keeps QML scope/binding-context paths stable across the
    // popup's first show. Without this, declared children become
    // direct children of the PopupWindow QQuickItem, get reparented to
    // the popup's window contentItem on first show, and bindings that
    // walk back through `root.<...>` lose their property-change
    // notifications post-reparent — manifests as content not updating
    // when the host swaps state while the popup stays mapped.
    Q_PROPERTY(QQmlListProperty<QObject> data READ data)
    Q_CLASSINFO("DefaultProperty", "data")

    Q_PROPERTY(QQuickItem* anchor READ anchor WRITE setAnchor NOTIFY anchorChanged)
    Q_PROPERTY(int popupWidth READ popupWidth WRITE setPopupWidth NOTIFY popupWidthChanged)
    Q_PROPERTY(int popupHeight READ popupHeight WRITE setPopupHeight NOTIFY popupHeightChanged)
    Q_PROPERTY(PopupEdge popupEdge READ popupEdge WRITE setPopupEdge NOTIFY popupEdgeChanged)
    Q_PROPERTY(int gap READ gap WRITE setGap NOTIFY gapChanged)
    Q_PROPERTY(bool popupVisible READ isPopupVisible WRITE setPopupVisible NOTIFY popupVisibleChanged)

public:
    enum PopupEdge {
        Below,
        Above,
        LeftOf,
        RightOf,
    };
    Q_ENUM(PopupEdge)

    explicit PopupWindow(QQuickItem* parent = nullptr);
    ~PopupWindow() override;

    /// QML default-property accessor. Forwards to m_contentItem's
    /// `data` property so QML children land on the persistent content
    /// holder rather than on PopupWindow itself.
    [[nodiscard]] QQmlListProperty<QObject> data();

    [[nodiscard]] QQuickItem* anchor() const;
    void setAnchor(QQuickItem* anchor);

    [[nodiscard]] int popupWidth() const;
    void setPopupWidth(int width);

    [[nodiscard]] int popupHeight() const;
    void setPopupHeight(int height);

    [[nodiscard]] PopupEdge popupEdge() const;
    void setPopupEdge(PopupEdge edge);

    [[nodiscard]] int gap() const;
    void setGap(int gap);

    [[nodiscard]] bool isPopupVisible() const;
    void setPopupVisible(bool visible);

    Q_INVOKABLE void close();

Q_SIGNALS:
    void anchorChanged();
    void popupWidthChanged();
    void popupHeightChanged();
    void popupEdgeChanged();
    void gapChanged();
    void popupVisibleChanged();

private:
    void showPopup();
    void hidePopup();
    [[nodiscard]] QRect computeAnchorRect() const;
    /// Re-apply the xdg-positioner / size when a live property change
    /// requires re-showing the popup. With xdg-shell ≥ v3 (KWin, Mutter,
    /// wlroots all support this) we issue xdg_popup.reposition on the
    /// existing popup proxy instead of destroying and recreating it,
    /// which preserves the grab and avoids the popup-switching race.
    /// Falls back to hide+show on older compositors.
    void reapplyIfVisible();
    /// Issue xdg_popup.reposition with a freshly-built positioner that
    /// reflects current anchor/edge/gap/size. Returns true if the
    /// reposition request was sent; false if no live xdg_popup exists
    /// (e.g. popup hidden) or xdg-shell version < 3.
    bool repositionInPlace();

    // Externally owned anchor item — QPointer lets us detect destruction
    // (the QML author may delete the anchor while the popup is alive).
    QPointer<QQuickItem> m_anchor;
    int m_popupWidth = 200;
    int m_popupHeight = 200;
    PopupEdge m_popupEdge = Below;
    int m_gap = 4;
    bool m_popupVisible = false;
    std::unique_ptr<QQuickWindow> m_popupWindow;
    /// Persistent host for QML-declared children. Created in the
    /// constructor and re-parented to m_popupWindow->contentItem() once
    /// on first show. Children added via the QML default property never
    /// move from m_contentItem after that, so their binding contexts
    /// remain stable across state changes that previously triggered a
    /// hide+show cycle.
    QQuickItem* m_contentItem = nullptr;
};

} // namespace PhosphorShell
