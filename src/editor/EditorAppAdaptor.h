// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusAbstractAdaptor>
#include <QString>

namespace PlasmaZones {

class EditorLaunchController;

/**
 * @brief D-Bus adaptor for the editor's single-instance launch surface.
 *
 * Implements the `org.plasmazones.EditorController` interface documented in
 * `dbus/org.plasmazones.EditorApp.xml`. Forwards calls to the parent
 * `EditorLaunchController` so the controller itself doesn't need
 * `Q_SCRIPTABLE` annotations polluting its signature.
 *
 * Lifetime: constructed as a child of the launch controller; automatically
 * destroyed with it. Qt D-Bus discovers the adaptor on
 * `QDBusConnection::registerObject` with the default `ExportAdaptors` flag.
 */
class EditorAppAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.EditorController")

public:
    explicit EditorAppAdaptor(EditorLaunchController* launcher);
    ~EditorAppAdaptor() override;

public Q_SLOTS:
    void handleLaunchRequest(const QString& screenId, const QString& layoutId, bool createNew, bool preview);

private:
    EditorLaunchController* m_launcher; ///< Non-owning; parent object.
};

} // namespace PlasmaZones
