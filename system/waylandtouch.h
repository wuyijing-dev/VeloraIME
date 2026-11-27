#pragma once

#include <QEventPoint>
#include <QMap>
#include <QPointingDevice>

class QWindow;

class WaylandTouch
{
public:
    explicit WaylandTouch(QWindow &window);

    bool hasContacts() const { return !m_contacts.isEmpty(); }
    void down(int wireId, const QPointF &position);
    void move(int wireId, const QPointF &position);
    void up(int wireId);
    bool dispatchFrame();
    void cancel();

private:
    struct Contact {
        int pointId;
        QPointF position;
        QEventPoint::State state;
    };
    static constexpr int maxContacts = 16;
    int availablePointId() const;

    QWindow &m_window;
    QPointingDevice m_device;
    QMap<int, Contact> m_contacts;
};
