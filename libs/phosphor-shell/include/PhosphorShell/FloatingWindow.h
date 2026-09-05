// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QQuickItem>
#include <QString>

#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorShell {

// A plain xdg toplevel driven from QML (or from C++ with the same
// surface). Children declared on it are moved into the toplevel's own
// QQuickWindow, so the item is a description of a window rather than a
// place in the scene.
//
// The compositor places, sizes and stacks it like any application window,
// which is the point: an engine-placed shell pane (A2 §4) IS one of these,
// with `appId` naming it to the daemon's window rules and `minimumWidth` /
// `minimumHeight` telling tiling and scrolling how small a slot it accepts.
class PHOSPHORSHELL_EXPORT FloatingWindow : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowWidthChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowHeightChanged)
    Q_PROPERTY(bool windowVisible READ isWindowVisible WRITE setWindowVisible NOTIFY windowVisibleChanged)
    // The toplevel's Wayland app_id, set PER WINDOW through Qt's shell
    // surface rather than process-wide through
    // QGuiApplication::setDesktopFileName, so one process can own windows
    // with different identities (the shell's panes each carry their own).
    // Empty keeps Qt's default (the desktop file name, else the binary
    // name). Applied when the window maps; changing it afterwards re-sends
    // it, which xdg-shell permits but most compositors only read at map.
    Q_PROPERTY(QString appId READ appId WRITE setAppId NOTIFY appIdChanged)
    // xdg_toplevel min size hints. 0 means unconstrained on that axis.
    Q_PROPERTY(int minimumWidth READ minimumWidth WRITE setMinimumWidth NOTIFY minimumWidthChanged)
    Q_PROPERTY(int minimumHeight READ minimumHeight WRITE setMinimumHeight NOTIFY minimumHeightChanged)
    // Ask the compositor for no server-side decoration: a frameless
    // toplevel draws its own edge. A shell pane sets this so it reads as a
    // tile, not a titled window. Read at window creation, so set it before
    // the first show.
    Q_PROPERTY(bool frameless READ isFrameless WRITE setFrameless NOTIFY framelessChanged)

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

    [[nodiscard]] QString appId() const;
    void setAppId(const QString& appId);

    [[nodiscard]] int minimumWidth() const;
    void setMinimumWidth(int width);

    [[nodiscard]] int minimumHeight() const;
    void setMinimumHeight(int height);

    [[nodiscard]] bool isFrameless() const;
    void setFrameless(bool frameless);

    // The toplevel itself, or null before the first show. Not named
    // `window()`: QQuickItem::window() is the scene this ITEM lives in,
    // which for a FloatingWindow is unrelated to the toplevel it owns.
    [[nodiscard]] QQuickWindow* quickWindow() const;

Q_SIGNALS:
    void titleChanged();
    void windowWidthChanged();
    void windowHeightChanged();
    void windowVisibleChanged();
    void appIdChanged();
    void minimumWidthChanged();
    void minimumHeightChanged();
    void framelessChanged();
    // The compositor closed the toplevel (xdg_toplevel.close, which Qt
    // delivers as a close event and hides the window). `windowVisible` is
    // already false when this fires. A compositor-placed pane needs this:
    // the engine's "close window" verb reaches it exactly this way, and
    // without the report the owner would keep a handle for a window that
    // is gone.
    void windowClosedByCompositor();

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void ensureWindow();
    void reparentChildToWindow(QQuickItem* child);
    void applyAppId();
    void applyMinimumSize();

    QString m_title;
    QString m_appId;
    int m_windowWidth = 400;
    int m_windowHeight = 300;
    int m_minimumWidth = 0;
    int m_minimumHeight = 0;
    bool m_frameless = false;
    bool m_windowVisible = false;
    std::unique_ptr<QQuickWindow> m_window;
};

} // namespace PhosphorShell
