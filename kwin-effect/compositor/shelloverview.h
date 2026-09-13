// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QVariantAnimation>
namespace KWin {
class Effect;
class EffectWindow;
class LogicalOutput;
class WindowPaintData;
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
    void transform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;
public Q_SLOTS:
    bool begin(const QString& screen, double x, double y, double width, double height, bool animate);
    void end();

private:
    void restore();
    KWin::Effect* m_effect;
    QPointer<KWin::LogicalOutput> m_output;
    QDBusServiceWatcher m_ownerWatcher;
    QString m_owner;
    QVariantAnimation m_animation;
    QRectF m_rect;
    bool m_closing = false;
};
}
