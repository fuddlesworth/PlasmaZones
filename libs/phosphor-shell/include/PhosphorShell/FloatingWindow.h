// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QQuickItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT FloatingWindow : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FloatingWindow)

    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowWidthChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowHeightChanged)
    Q_PROPERTY(bool windowVisible READ isWindowVisible WRITE setWindowVisible NOTIFY windowVisibleChanged)

public:
    explicit FloatingWindow(QQuickItem* parent = nullptr);
    ~FloatingWindow() override;

    [[nodiscard]] QString title() const;
    void setTitle(const QString& title);

    [[nodiscard]] int windowWidth() const;
    void setWindowWidth(int width);

    [[nodiscard]] int windowHeight() const;
    void setWindowHeight(int height);

    [[nodiscard]] bool isWindowVisible() const;
    void setWindowVisible(bool visible);

Q_SIGNALS:
    void titleChanged();
    void windowWidthChanged();
    void windowHeightChanged();
    void windowVisibleChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void ensureWindow();
    void reparentChildToWindow(QQuickItem* child);

    QString m_title;
    int m_windowWidth = 400;
    int m_windowHeight = 300;
    bool m_windowVisible = false;
    std::unique_ptr<QQuickWindow> m_window;
};

} // namespace PhosphorShell
