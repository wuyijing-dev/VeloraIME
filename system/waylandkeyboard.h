#pragma once

#include "keyboardcontroller.h"
#include "waylandtouch.h"

#include <QEvent>
#include <QImage>
#include <QPointF>
#include <QPointer>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QTimer>
#include <QVariant>

#include <memory>
#include <deque>
#include <vector>

struct wl_buffer;
struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_pointer;
struct wl_registry;
struct wl_seat;
struct wl_shm;
struct wl_surface;
struct wl_touch;
struct zwp_input_method_context_v1;
struct zwp_input_method_v1;
struct zwp_input_panel_surface_v1;
struct zwp_input_panel_v1;
class QQuickItem;

class WaylandKeyboard final : public KeyboardInputTarget
{
    Q_OBJECT

public:
    WaylandKeyboard();
    ~WaylandKeyboard() override;

    bool initialize();
    void attach(QQuickItem *panel);
    bool active() const override;
    bool hasDeferredContext() const override { return true; }
    QVariant query(Qt::InputMethodQuery query) const override;
    bool commit(const QString &text, int replacementStart, int replacementLength) override;
    void preedit(const QString &text) override;
    void key(int key, Qt::KeyboardModifiers modifiers, const QString &text) override;

public slots:
    void hidePanel();

private:
    struct Frame;
    struct SurroundingText {
        QString text;
        qsizetype cursor = -1;
        qsizetype anchor = -1;

        bool known() const { return cursor >= 0 && anchor >= 0; }
        bool operator==(const SurroundingText &other) const {
            return text == other.text && cursor == other.cursor && anchor == other.anchor;
        }
    };
    void bindGlobal(wl_registry *registry, uint32_t name, const char *type, uint32_t version);
    void updateSeat(uint32_t capabilities);
    void activate(zwp_input_method_context_v1 *context);
    void deactivate(zwp_input_method_context_v1 *context);
    void updateHints(uint32_t hints, uint32_t purpose);
    void setSurrounding(const char *text, uint32_t cursor, uint32_t anchor);
    void forgetSurrounding();
    void updateFocus();
    void scheduleRender();
    void wakeRenderer();
    void render();
    std::unique_ptr<Frame> createFrame(const QSize &size);
    void releaseFrame(Frame *frame);
    void pointerEvent(QEvent::Type type, QPointF position);
    void cancelPointer();
    void cancelInput();
    void flush();

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_shm *m_shm = nullptr;
    wl_output *m_output = nullptr;
    wl_seat *m_seat = nullptr;
    wl_pointer *m_pointer = nullptr;
    wl_touch *m_touch = nullptr;
    wl_surface *m_surface = nullptr;
    zwp_input_method_v1 *m_method = nullptr;
    zwp_input_panel_v1 *m_inputPanel = nullptr;
    zwp_input_panel_surface_v1 *m_panelSurface = nullptr;
    zwp_input_method_context_v1 *m_context = nullptr;
    uint32_t m_serial = 0;
    QSize m_outputMode{800, 480};
    int m_outputTransform = 0;
    int m_outputScale = 1;
    bool m_ready = false;
    bool m_hidden = false;
    bool m_pointerInside = false;
    bool m_pressed = false;
    bool m_renderDirty = false;
    QPointF m_pointerPosition;
    Qt::InputMethodHints m_hints = Qt::ImhSensitiveData;
    SurroundingText m_surrounding;
    std::deque<SurroundingText> m_expectedSurrounding;
    QTimer m_surroundingTimeout;
    QTimer m_contextNotification;
    QQuickRenderControl m_renderControl;
    QQuickWindow m_window;
    WaylandTouch m_nativeTouch;
    QPointer<QQuickItem> m_panel;
    QTimer m_renderTimer;
    QImage m_image;
    std::vector<std::unique_ptr<Frame>> m_frames;
};
