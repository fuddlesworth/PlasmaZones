// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT PopupWindow : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PopupWindow)

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

    QQuickItem* anchor() const;
    void setAnchor(QQuickItem* anchor);

    int popupWidth() const;
    void setPopupWidth(int width);

    int popupHeight() const;
    void setPopupHeight(int height);

    PopupEdge popupEdge() const;
    void setPopupEdge(PopupEdge edge);

    int gap() const;
    void setGap(int gap);

    bool isPopupVisible() const;
    void setPopupVisible(bool visible);

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
    QRect computeAnchorRect() const;

    QQuickItem* m_anchor = nullptr;
    int m_popupWidth = 200;
    int m_popupHeight = 200;
    PopupEdge m_popupEdge = Below;
    int m_gap = 4;
    bool m_popupVisible = false;
    QQuickWindow* m_popupWindow = nullptr;
};

} // namespace PhosphorShell
