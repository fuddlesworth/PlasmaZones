// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <PhosphorWayland/ForeignToplevel.h>

#include <QList>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace PhosphorShell {

/**
 * @brief QML singleton exposing the live list of toplevel windows for taskbars.
 *
 * Wraps `PhosphorWayland::ForeignToplevelManager` and turns the imperative
 * `toplevelAdded`/`toplevelRemoved` signals into a notifying `toplevels`
 * Q_PROPERTY suitable for `Repeater { model: Toplevels.toplevels }`.
 *
 * Usage from QML:
 *
 *     import Phosphor.Shell
 *
 *     Repeater {
 *         model: Toplevels.toplevels
 *         delegate: Rectangle {
 *             required property var modelData  // ForeignToplevel*
 *             Text { text: modelData.title }
 *             MouseArea {
 *                 anchors.fill: parent
 *                 onClicked: modelData.activate()
 *             }
 *         }
 *     }
 *
 * The `supported` property is `false` on compositors that don't advertise
 * `zwlr_foreign_toplevel_manager_v1` (e.g. plain Mutter without extensions);
 * shells should hide their taskbar UI in that case rather than showing an
 * empty placeholder. KWin and the wlroots family (sway, Hyprland, Niri,
 * river, labwc) all support it.
 */
class PHOSPHORSHELL_EXPORT Toplevels : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Toplevels)
    QML_SINGLETON

    Q_PROPERTY(QList<PhosphorWayland::ForeignToplevel*> toplevels READ toplevels NOTIFY toplevelsChanged)
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

public:
    explicit Toplevels(QObject* parent = nullptr);
    ~Toplevels() override;

    /// QML-singleton factory hook.
    static Toplevels* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    [[nodiscard]] QList<PhosphorWayland::ForeignToplevel*> toplevels() const;
    [[nodiscard]] bool isSupported() const;

Q_SIGNALS:
    void toplevelsChanged();

private:
    std::unique_ptr<PhosphorWayland::ForeignToplevelManager> m_manager;
};

} // namespace PhosphorShell
