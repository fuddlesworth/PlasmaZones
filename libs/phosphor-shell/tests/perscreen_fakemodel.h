// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Header-only fake screen model for PerScreen tests. Mirrors the role
// layout from PhosphorShell::ScreenModel — same enum values, same
// roleNames() — so PerScreen.qml's hardcoded role integers
// (Qt.UserRole + N) line up without dragging the real ScreenModel /
// IScreenProvider stack into the test. Mock "screens" are plain
// QObject* pointers parented to the model; the pointer identity is
// what PerScreen's Map keys delegates on, so any QObject* works.

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtCore/qtclasshelpermacros.h>

namespace PhosphorShellTests {

class FakeScreenModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        ScreenRole = Qt::UserRole + 1,
        NameRole,
        WidthRole,
        HeightRole,
        IsPrimaryRole,
    };

    explicit FakeScreenModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
    }
    Q_DISABLE_COPY_MOVE(FakeScreenModel)

    // Allocate a fresh mock screen QObject parented to this model.
    // Returned pointer is owned by the model; it stays valid until
    // explicitly removed (removeScreenAt / setScreens to a list that
    // doesn't reference it) or until the model is destroyed. Callers
    // that don't need the pointer back can discard the return; the
    // mock screen is reachable through `data(index, ScreenRole)`.
    QObject* makeScreen(const QString& name, int width = 1920, int height = 1080, bool isPrimary = false)
    {
        auto* screen = new QObject(this);
        screen->setObjectName(name);
        m_entries.append({screen, name, width, height, isPrimary});
        return screen;
    }

    // Trigger a full reset to the supplied screen set. PerScreen
    // observes via the modelReset signal; survivors (screens already
    // in m_entries that also appear in `entries`) should keep their
    // delegate identity through the reset.
    void setScreens(const QList<QObject*>& screens, const QStringList& names = {})
    {
        beginResetModel();
        QList<Entry> next;
        next.reserve(screens.size());
        for (int i = 0; i < screens.size(); ++i) {
            QObject* screen = screens.at(i);
            const Entry existing = findExisting(screen);
            Entry e = existing.screen ? existing : Entry{screen, screen->objectName(), 1920, 1080, false};
            if (i < names.size()) {
                e.name = names.at(i);
            }
            next.append(e);
        }
        m_entries = next;
        endResetModel();
    }

    // Hot-plug add: a fresh screen joins the model via reset.
    QObject* addScreen(const QString& name, bool isPrimary = false)
    {
        beginResetModel();
        auto* screen = new QObject(this);
        screen->setObjectName(name);
        m_entries.append({screen, name, 1920, 1080, isPrimary});
        endResetModel();
        return screen;
    }

    // Hot-plug remove via reset.
    void removeScreen(QObject* screen)
    {
        beginResetModel();
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).screen == screen) {
                m_entries.removeAt(i);
                screen->deleteLater();
                break;
            }
        }
        endResetModel();
    }

    // Primary-screen swap: a dataChanged signal over the entire model
    // with IsPrimaryRole. PerScreen must update isPrimary in place
    // without recreating delegates.
    void setPrimary(QObject* screen)
    {
        for (int i = 0; i < m_entries.size(); ++i) {
            m_entries[i].isPrimary = (m_entries.at(i).screen == screen);
        }
        if (!m_entries.isEmpty()) {
            Q_EMIT dataChanged(index(0), index(m_entries.size() - 1), {IsPrimaryRole});
        }
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : m_entries.size();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
            return {};
        }
        const Entry& e = m_entries.at(index.row());
        switch (role) {
        case ScreenRole:
            return QVariant::fromValue(e.screen);
        case NameRole:
            return e.name;
        case WidthRole:
            return e.width;
        case HeightRole:
            return e.height;
        case IsPrimaryRole:
            return e.isPrimary;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {ScreenRole, "screen"}, {NameRole, "name"},           {WidthRole, "width"},
            {HeightRole, "height"}, {IsPrimaryRole, "isPrimary"},
        };
    }

private:
    struct Entry
    {
        QObject* screen = nullptr;
        QString name;
        int width = 0;
        int height = 0;
        bool isPrimary = false;
    };

    Entry findExisting(QObject* screen) const
    {
        for (const Entry& e : m_entries) {
            if (e.screen == screen) {
                return e;
            }
        }
        return {};
    }

    QList<Entry> m_entries;
};

} // namespace PhosphorShellTests
