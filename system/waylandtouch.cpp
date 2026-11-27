#include "waylandtouch.h"

#include <QCoreApplication>
#include <QTouchEvent>
#include <QWindow>
#include <private/qhighdpiscaling_p.h>
#include <private/qpointingdevice_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <array>
#include <utility>

WaylandTouch::WaylandTouch(QWindow &window)
    : m_window(window),
      m_device(QStringLiteral("Weston touchscreen"), 0x56454c4f,
               QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger,
               QInputDevice::Capability::Position, maxContacts, 0)
{
    QWindowSystemInterface::registerInputDevice(&m_device);
}

void WaylandTouch::down(int wireId, const QPointF &position)
{
    const auto existing = m_contacts.constFind(wireId);
    if (existing != m_contacts.cend() && existing->state == QEventPoint::Released)
        dispatchFrame();
    if (m_contacts.contains(wireId) || m_contacts.size() >= maxContacts)
        return;
    m_contacts.insert(wireId, {availablePointId(), position, QEventPoint::Pressed});
}

void WaylandTouch::move(int wireId, const QPointF &position)
{
    auto contact = m_contacts.find(wireId);
    if (contact == m_contacts.end() || contact->state == QEventPoint::Released)
        return;
    contact->position = position;
    if (contact->state != QEventPoint::Pressed)
        contact->state = QEventPoint::Updated;
}

void WaylandTouch::up(int wireId)
{
    auto contact = m_contacts.find(wireId);
    if (contact == m_contacts.end())
        return;
    if (contact->state == QEventPoint::Pressed) {
        dispatchFrame();
        contact = m_contacts.find(wireId);
    }
    if (contact != m_contacts.end())
        contact->state = QEventPoint::Released;
}

bool WaylandTouch::dispatchFrame()
{
    bool changed = false;
    QList<QWindowSystemInterface::TouchPoint> points;
    points.reserve(m_contacts.size());
    for (auto it = m_contacts.begin(); it != m_contacts.end();) {
        const auto &contact = it.value();
        QWindowSystemInterface::TouchPoint point;
        point.id = contact.pointId;
        point.state = contact.state;
        point.pressure = contact.state == QEventPoint::Released ? 0 : 1;
        const QPointF global = QPointF(m_window.mapToGlobal(QPoint())) + contact.position;
        point.area = QHighDpi::toNativePixels(QRectF(global - QPointF(1, 1), QSizeF(2, 2)), &m_window);
        points.append(point);
        changed = changed || contact.state != QEventPoint::Stationary;
        if (contact.state == QEventPoint::Released)
            it = m_contacts.erase(it);
        else
            (it++)->state = QEventPoint::Stationary;
    }

    if (changed)
        QWindowSystemInterface::handleTouchEvent<QWindowSystemInterface::SynchronousDelivery>(
            &m_window, &m_device, points);
    return changed;
}

void WaylandTouch::cancel()
{
    if (m_contacts.isEmpty())
        return;
    const auto contacts = std::exchange(m_contacts, {});
    auto *device = QPointingDevicePrivate::get(&m_device);
    QList<QEventPoint> points;
    for (const auto &contact : contacts) {
        if (const auto *point = device->queryPointById(contact.pointId))
            points.append(point->eventPoint);
    }
    QTouchEvent cancel(QEvent::TouchCancel, &m_device, Qt::NoModifier, points);
    // Qt 6.12 also reports ordinary ungrabs on TouchCancel. Notify handlers
    // directly so they can distinguish cancellation from release.
    for (const auto &eventPoint : points) {
        auto *point = device->queryPointById(eventPoint.id());
        if (!point)
            continue;
        const auto exclusive = std::exchange(point->exclusiveGrabber, {});
        point->exclusiveGrabberContext.clear();
        const auto passive = std::exchange(point->passiveGrabbers, {});
        point->passiveGrabbersContext.clear();
        if (exclusive) {
            QTouchEvent pointCancel(QEvent::TouchCancel, &m_device, Qt::NoModifier, {eventPoint});
            if (!QCoreApplication::sendEvent(exclusive, &pointCancel) && exclusive)
                emit m_device.grabChanged(exclusive, QPointingDevice::CancelGrabExclusive,
                                          &pointCancel, eventPoint);
        }
        for (const auto &grabber : passive) {
            if (grabber && grabber != exclusive)
                emit m_device.grabChanged(grabber, QPointingDevice::CancelGrabPassive, &cancel, eventPoint);
        }
    }
    QWindowSystemInterface::handleTouchCancelEvent<QWindowSystemInterface::SynchronousDelivery>(
        &m_window, &m_device);
}

int WaylandTouch::availablePointId() const
{
    std::array<bool, maxContacts> occupied{};
    for (const auto &contact : m_contacts)
        occupied.at(contact.pointId) = true;
    for (int i = 0; i < maxContacts; ++i) {
        if (!occupied.at(i))
            return i;
    }
    Q_UNREACHABLE();
}
