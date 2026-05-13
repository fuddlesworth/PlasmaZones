// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <PhosphorServices/MprisPlayer.h>

#include <QList>
#include <QObject>

namespace PhosphorServices {

class PHOSPHORSERVICES_EXPORT MprisHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int playerCount READ playerCount NOTIFY playerCountChanged)

public:
    explicit MprisHost(QObject* parent = nullptr);
    ~MprisHost() override;

    [[nodiscard]] int playerCount() const;
    [[nodiscard]] QList<MprisPlayer*> players() const;
    [[nodiscard]] Q_INVOKABLE PhosphorServices::MprisPlayer* playerAt(int index) const;

Q_SIGNALS:
    void playerAdded(PhosphorServices::MprisPlayer* player);
    void playerRemoved(PhosphorServices::MprisPlayer* player);
    void playerCountChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServices
