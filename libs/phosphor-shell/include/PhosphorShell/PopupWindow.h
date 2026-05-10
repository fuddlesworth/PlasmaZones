// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QPointer>
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

Q_SIGNALS:
    void anchorChanged();
    void popupWidthChanged();
    void popupHeightChanged();
    void popupEdgeChanged();
    void gapChanged();
    void popupVisibleChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void showPopup();
    void hidePopup();
    [[nodiscard]] QRect computeAnchorRect() const;
    void reparentChildToWindow(QQuickItem* child);
    /// Re-apply the xdg-positioner / size when a live property change
    /// requires re-showing the popup (anchor, edge, gap, geometry only
    /// take effect at popup creation time per xdg-shell semantics).
    void reapplyIfVisible();

    // Externally owned anchor item — QPointer lets us detect destruction
    // (the QML author may delete the anchor while the popup is alive).
    QPointer<QQuickItem> m_anchor;
    int m_popupWidth = 200;
    int m_popupHeight = 200;
    PopupEdge m_popupEdge = Below;
    int m_gap = 4;
    bool m_popupVisible = false;
    std::unique_ptr<QQuickWindow> m_popupWindow;
};

} // namespace PhosphorShell
