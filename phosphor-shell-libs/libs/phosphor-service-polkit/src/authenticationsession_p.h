// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePolkit/phosphorservicepolkit_export.h>

#include <QObject>
#include <QString>

namespace PhosphorServicePolkit::detail {

// Private conversation boundary. The production adapter owns a polkit-qt
// session; isolated lifecycle tests supply a session that never invokes PAM.
class PHOSPHORSERVICEPOLKIT_EXPORT AuthenticationSession : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void initiate() = 0;
    virtual void respond(const QString& response) = 0;
    virtual void cancel() = 0;

Q_SIGNALS:
    void requested(const QString& prompt, bool echo);
    void completed(bool gainedAuthorization);
    void error(const QString& text);
    void info(const QString& text);
};

} // namespace PhosphorServicePolkit::detail
