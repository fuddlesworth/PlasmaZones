// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusContext>
#include <QHash>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QVariantAnimation>
#include <optional>
namespace KWin {
class Effect;
class EffectWindow;
class LogicalOutput;
class WindowPaintData;
class RenderTarget;
class RenderViewport;
class Region;
}
namespace PlasmaZones {
// Owns the compositor half of Stage. A lost shell connection, removed
// output, or effect unload always restores the ordinary desktop transform.
class ShellOverview : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.ShellOverview")
public:
    explicit ShellOverview(KWin::Effect* effect);
    ~ShellOverview() override;
    bool active() const;
    bool onOutput(KWin::LogicalOutput* output) const;
    bool appliesTo(KWin::EffectWindow* window) const;
    // Shared identity/focus source for native titlebars, surface shaders and
    // overview cards. Shell popups keep the last application visually focused.
    std::optional<int> windowColorIndex(KWin::EffectWindow* window) const;
    bool windowFocused(const QString& windowId) const;
    void transform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;
    void paint(const KWin::RenderTarget& target, const KWin::RenderViewport& viewport, KWin::EffectWindow* window,
               int mask, const KWin::Region& region, KWin::WindowPaintData& data) const;
public Q_SLOTS:
    bool begin(const QString& screen, double x, double y, double width, double height, bool animate,
               const QString& token, double clipX, double clipY, double clipWidth, double clipHeight);
    void end(const QString& token);
    QString windows(const QString& screen, int desktop) const;

Q_SIGNALS:
    void windowsChanged();

private:
    void restore();
    void updateDecorations();
    void watchWindow(KWin::EffectWindow* window);
    QPointF visualOffset(KWin::EffectWindow* window, bool animated) const;
    KWin::Effect* m_effect;
    QPointer<KWin::LogicalOutput> m_output;
    QDBusServiceWatcher m_ownerWatcher;
    QString m_owner;
    QString m_token;
    QVariantAnimation m_animation;
    QRectF m_rect;
    QRectF m_targetRect;
    QRectF m_clipRect;
    bool m_closing = false;
    QTimer m_windowChanges;
    QString m_lastFocusedWindow;
    // Stable for a window's lifetime; closing or reordering another window
    // must not recolour surviving titlebars, surface shaders or overview cards.
    QHash<KWin::EffectWindow*, int> m_windowColors;
    int m_nextColor = 0;
};
}
