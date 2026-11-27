#include "waylandkeyboard.h"

#include "input-method-client.h"
#include "text-input-client.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickRenderTarget>
#include <QSocketNotifier>
#include <QStringDecoder>
#include <QTemporaryFile>

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iterator>

namespace {
constexpr auto privacyHints = Qt::ImhHiddenText | Qt::ImhSensitiveData;
constexpr qsizetype maxPendingEdits = 32;
constexpr qsizetype maxSurroundingBytes = 16 * 1024;

bool isUtf8Boundary(const QByteArray &text, qsizetype offset)
{
    return offset == text.size()
        || (offset >= 0 && offset < text.size() && (uchar(text.at(offset)) & 0xc0) != 0x80);
}

bool isUtf16Boundary(const QString &text, qsizetype offset)
{
    return offset >= 0 && offset <= text.size()
        && !(offset > 0 && offset < text.size() && text.at(offset).isLowSurrogate()
                  && text.at(offset - 1).isHighSurrogate());
}
}

struct WaylandKeyboard::Frame
{
    WaylandKeyboard *owner = nullptr;
    wl_buffer *buffer = nullptr;
    void *memory = MAP_FAILED;
    qsizetype bytes = 0;
    QSize size;
    bool busy = false;

    ~Frame()
    {
        if (buffer)
            wl_buffer_destroy(buffer);
        if (memory != MAP_FAILED)
            munmap(memory, size_t(bytes));
    }
};

WaylandKeyboard::WaylandKeyboard()
    : m_window(&m_renderControl), m_nativeTouch(m_window)
{
    m_window.setColor(Qt::transparent);
    m_renderTimer.setSingleShot(true);
    connect(&m_renderTimer, &QTimer::timeout, this, &WaylandKeyboard::render);
    connect(&m_renderControl, &QQuickRenderControl::renderRequested,
            this, &WaylandKeyboard::scheduleRender);
    connect(&m_renderControl, &QQuickRenderControl::sceneChanged,
            this, &WaylandKeyboard::scheduleRender);
    m_contextNotification.setSingleShot(true);
    connect(&m_contextNotification, &QTimer::timeout, this, &KeyboardInputTarget::contextChanged);
    m_surroundingTimeout.setSingleShot(true);
    m_surroundingTimeout.setInterval(500);
    connect(&m_surroundingTimeout, &QTimer::timeout, this, [this] {
        forgetSurrounding();
        m_contextNotification.start(0);
    });
}

WaylandKeyboard::~WaylandKeyboard()
{
    m_window.setRenderTarget({});
    m_frames.clear();
    if (m_context)
        zwp_input_method_context_v1_destroy(m_context);
    if (m_panelSurface)
        zwp_input_panel_surface_v1_destroy(m_panelSurface);
    if (m_surface)
        wl_surface_destroy(m_surface);
    if (m_method)
        zwp_input_method_v1_destroy(m_method);
    if (m_inputPanel)
        zwp_input_panel_v1_destroy(m_inputPanel);
    if (m_pointer)
        wl_pointer_destroy(m_pointer);
    if (m_touch)
        wl_touch_destroy(m_touch);
    if (m_seat)
        wl_seat_destroy(m_seat);
    if (m_output)
        wl_output_destroy(m_output);
    if (m_shm)
        wl_shm_destroy(m_shm);
    if (m_compositor)
        wl_compositor_destroy(m_compositor);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_disconnect(m_display);
}

bool WaylandKeyboard::initialize()
{
    m_display = wl_display_connect(nullptr);
    if (!m_display)
        return false;

    static const wl_registry_listener registryListener{
        [](void *data, wl_registry *registry, uint32_t name, const char *type, uint32_t version) {
            static_cast<WaylandKeyboard *>(data)->bindGlobal(registry, name, type, version);
        },
        [](void *, wl_registry *, uint32_t) {}
    };
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registryListener, this);
    if (wl_display_roundtrip(m_display) < 0 || wl_display_roundtrip(m_display) < 0
        || !m_compositor || !m_shm || !m_output || !m_seat || !m_method || !m_inputPanel) {
        return false;
    }

    m_surface = wl_compositor_create_surface(m_compositor);
    m_panelSurface = zwp_input_panel_v1_get_input_panel_surface(m_inputPanel, m_surface);
    zwp_input_panel_surface_v1_set_toplevel(m_panelSurface, m_output,
                                            ZWP_INPUT_PANEL_SURFACE_V1_POSITION_CENTER_BOTTOM);
    auto *socket = new QSocketNotifier(wl_display_get_fd(m_display), QSocketNotifier::Read, this);
    connect(socket, &QSocketNotifier::activated, this, [this, socket] {
        if (wl_display_dispatch(m_display) < 0) {
            socket->setEnabled(false);
            QCoreApplication::exit(1);
            return;
        }
        flush();
    });
    auto *writeSocket = new QSocketNotifier(wl_display_get_fd(m_display), QSocketNotifier::Write, this);
    writeSocket->setObjectName(QStringLiteral("waylandWrite"));
    writeSocket->setEnabled(false);
    connect(writeSocket, &QSocketNotifier::activated, this, [this] { flush(); });
    flush();
    return true;
}

void WaylandKeyboard::attach(QQuickItem *panel)
{
    m_panel = panel;
    panel->setParentItem(m_window.contentItem());
    connect(panel, &QQuickItem::implicitHeightChanged, this, &WaylandKeyboard::scheduleRender);
    // Keep a buffer on the surface so Weston can animate it as soon as a field opens.
    scheduleRender();
}

void WaylandKeyboard::flush()
{
    if (auto *notifier = findChild<QSocketNotifier *>(QStringLiteral("waylandWrite"))) {
        const int result = wl_display_flush(m_display);
        notifier->setEnabled(result < 0 && errno == EAGAIN);
        if (result < 0 && errno != EAGAIN)
            QCoreApplication::exit(1);
    }
}

void WaylandKeyboard::bindGlobal(wl_registry *registry, uint32_t name, const char *type, uint32_t version)
{
    if (strcmp(type, wl_compositor_interface.name) == 0) {
        m_compositor = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
    } else if (strcmp(type, wl_shm_interface.name) == 0) {
        m_shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(type, wl_output_interface.name) == 0 && !m_output) {
        static const wl_output_listener outListener{
            [](void *data, wl_output *, int32_t, int32_t, int32_t, int32_t, int32_t,
               const char *, const char *, int32_t transform) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                if (self->m_outputTransform != transform)
                    self->cancelInput();
                self->m_outputTransform = transform;
                self->scheduleRender();
            },
            [](void *data, wl_output *, uint32_t flags, int32_t width, int32_t height, int32_t) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                if (flags & WL_OUTPUT_MODE_CURRENT) {
                    self->m_outputMode = {width, height};
                    self->scheduleRender();
                }
            },
            [](void *data, wl_output *) { static_cast<WaylandKeyboard *>(data)->scheduleRender(); },
            [](void *data, wl_output *, int32_t scale) {
                static_cast<WaylandKeyboard *>(data)->m_outputScale = std::max(1, scale);
            },
            nullptr, nullptr
        };
        m_output = static_cast<wl_output *>(wl_registry_bind(registry, name, &wl_output_interface,
                                                             std::min(2u, version)));
        wl_output_add_listener(m_output, &outListener, this);
    } else if (strcmp(type, wl_seat_interface.name) == 0 && !m_seat) {
        static const wl_seat_listener seatListener{
            [](void *data, wl_seat *, uint32_t capabilities) {
                static_cast<WaylandKeyboard *>(data)->updateSeat(capabilities);
            },
            nullptr
        };
        m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        wl_seat_add_listener(m_seat, &seatListener, this);
    } else if (strcmp(type, zwp_input_method_v1_interface.name) == 0 && !m_method) {
        static const zwp_input_method_v1_listener methodListener{
            [](void *data, zwp_input_method_v1 *, zwp_input_method_context_v1 *context) {
                static_cast<WaylandKeyboard *>(data)->activate(context);
            },
            [](void *data, zwp_input_method_v1 *, zwp_input_method_context_v1 *context) {
                static_cast<WaylandKeyboard *>(data)->deactivate(context);
            }
        };
        m_method = static_cast<zwp_input_method_v1 *>(
            wl_registry_bind(registry, name, &zwp_input_method_v1_interface, 1));
        zwp_input_method_v1_add_listener(m_method, &methodListener, this);
    } else if (strcmp(type, zwp_input_panel_v1_interface.name) == 0) {
        m_inputPanel = static_cast<zwp_input_panel_v1 *>(
            wl_registry_bind(registry, name, &zwp_input_panel_v1_interface, 1));
    }
}

bool WaylandKeyboard::active() const
{
    return m_context && m_ready && !m_hidden;
}

QVariant WaylandKeyboard::query(Qt::InputMethodQuery property) const
{
    switch (property) {
    case Qt::ImEnabled:
        return active();
    case Qt::ImReadOnly:
        return false;
    case Qt::ImHints:
        return int(m_hints);
    default:
        break;
    }
    if (!m_surrounding.known() || !m_expectedSurrounding.empty() || m_hints.testAnyFlags(privacyHints))
        return {};

    switch (property) {
    case Qt::ImCurrentSelection:
        return m_surrounding.text.mid(std::min(m_surrounding.cursor, m_surrounding.anchor),
                                      std::abs(m_surrounding.cursor - m_surrounding.anchor));
    case Qt::ImCursorPosition:
        return int(m_surrounding.cursor);
    case Qt::ImAnchorPosition:
        return int(m_surrounding.anchor);
    case Qt::ImSurroundingText:
        return m_surrounding.text;
    case Qt::ImTextBeforeCursor:
        return m_surrounding.text.left(m_surrounding.cursor);
    case Qt::ImTextAfterCursor:
        return m_surrounding.text.mid(m_surrounding.cursor);
    default:
        return {};
    }
}

void WaylandKeyboard::activate(zwp_input_method_context_v1 *context)
{
    if (m_context)
        zwp_input_method_context_v1_destroy(m_context);
    m_context = context;
    m_ready = false;
    m_hidden = false;
    m_hints = Qt::ImhSensitiveData;
    m_serial = 0;
    forgetSurrounding();
    cancelInput();

    static const zwp_input_method_context_v1_listener contextListener{
        [](void *data, zwp_input_method_context_v1 *, const char *text, uint32_t cursor, uint32_t anchor) {
            static_cast<WaylandKeyboard *>(data)->setSurrounding(text, cursor, anchor);
        },
        [](void *data, zwp_input_method_context_v1 *) {
            auto *self = static_cast<WaylandKeyboard *>(data);
            self->forgetSurrounding();
            emit self->sessionChanged();
        },
        [](void *data, zwp_input_method_context_v1 *, uint32_t hints, uint32_t purpose) {
            static_cast<WaylandKeyboard *>(data)->updateHints(hints, purpose);
        },
        [](void *, zwp_input_method_context_v1 *, uint32_t, uint32_t) {},
        [](void *data, zwp_input_method_context_v1 *, uint32_t serial) {
            auto *self = static_cast<WaylandKeyboard *>(data);
            self->m_serial = serial;
            if (!self->m_ready) {
                self->m_ready = true;
                self->updateFocus();
            }
        },
        [](void *, zwp_input_method_context_v1 *, const char *) {}
    };
    zwp_input_method_context_v1_add_listener(context, &contextListener, this);
    char modifiers[] = "Shift\0Control\0Mod1";
    wl_array map{sizeof(modifiers), sizeof(modifiers), modifiers};
    zwp_input_method_context_v1_modifiers_map(context, &map);
    emit sessionChanged();
    updateFocus();
}

void WaylandKeyboard::deactivate(zwp_input_method_context_v1 *context)
{
    if (context != m_context)
        return;
    zwp_input_method_context_v1_destroy(context);
    m_context = nullptr;
    m_ready = false;
    forgetSurrounding();
    cancelInput();
    emit sessionChanged();
    updateFocus();
}

void WaylandKeyboard::updateHints(uint32_t hints, uint32_t purpose)
{
    Qt::InputMethodHints flags;
    if (hints & ZWP_TEXT_INPUT_V1_CONTENT_HINT_HIDDEN_TEXT)
        flags |= Qt::ImhHiddenText;
    if (hints & ZWP_TEXT_INPUT_V1_CONTENT_HINT_SENSITIVE_DATA)
        flags |= Qt::ImhSensitiveData;
    if (!(hints & ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_COMPLETION))
        flags |= Qt::ImhNoPredictiveText;
    if (!(hints & ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_CAPITALIZATION))
        flags |= Qt::ImhNoAutoUppercase;
    switch (purpose) {
    case ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_DIGITS: flags |= Qt::ImhDigitsOnly; break;
    case ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_NUMBER: flags |= Qt::ImhFormattedNumbersOnly; break;
    case ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PHONE: flags |= Qt::ImhDialableCharactersOnly; break;
    case ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_URL: flags |= Qt::ImhUrlCharactersOnly; break;
    case ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_EMAIL: flags |= Qt::ImhEmailCharactersOnly; break;
    case ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PASSWORD: flags |= Qt::ImhHiddenText | Qt::ImhSensitiveData; break;
    default: break;
    }
    m_hints = flags;
    if (m_hints.testAnyFlags(privacyHints))
        forgetSurrounding();
    emit stateChanged();
}

void WaylandKeyboard::setSurrounding(const char *text, uint32_t cursor, uint32_t anchor)
{
    if (m_hidden || m_hints.testAnyFlags(privacyHints))
        return;
    const QByteArray bytes(text);
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder(bytes);
    if (bytes.size() > maxSurroundingBytes || decoder.hasError()
        || !isUtf8Boundary(bytes, cursor) || !isUtf8Boundary(bytes, anchor)) {
        forgetSurrounding();
        m_contextNotification.start(0);
        return;
    }
    const SurroundingText received{decoded, QString::fromUtf8(bytes.constData(), cursor).size(),
                                   QString::fromUtf8(bytes.constData(), anchor).size()};
    const auto expected = std::find(m_expectedSurrounding.rbegin(), m_expectedSurrounding.rend(), received);
    if (expected != m_expectedSurrounding.rend()) {
        m_expectedSurrounding.erase(m_expectedSurrounding.begin(), expected.base());
    } else if (m_surrounding == received) {
        return;
    } else {
        m_expectedSurrounding.clear();
    }
    m_surrounding = received;
    if (m_expectedSurrounding.empty()) {
        m_surroundingTimeout.stop();
        m_contextNotification.start(0);
    }
}

void WaylandKeyboard::forgetSurrounding()
{
    m_surrounding = {};
    m_expectedSurrounding.clear();
    m_surroundingTimeout.stop();
    m_contextNotification.stop();
}

void WaylandKeyboard::updateFocus()
{
    emit stateChanged();
    scheduleRender();
}

bool WaylandKeyboard::commit(const QString &text, int replacementStart, int replacementLength)
{
    if (!active() || text.contains(u'\0') || replacementLength < 0)
        return false;
    const auto &latest = m_expectedSurrounding.empty() ? m_surrounding : m_expectedSurrounding.back();
    qsizetype start = std::min(latest.cursor, latest.anchor);
    qsizetype length = std::abs(latest.cursor - latest.anchor);
    if (replacementLength > 0) {
        start = latest.cursor + replacementStart;
        length = replacementLength;
        if (!latest.known() || !m_expectedSurrounding.empty() || latest.cursor != latest.anchor
            || !isUtf16Boundary(latest.text, start) || !isUtf16Boundary(latest.text, start + length))
            return false;
        const auto cursor = latest.text.left(latest.cursor).toUtf8().size();
        const auto position = latest.text.left(start).toUtf8().size();
        const auto bytes = latest.text.mid(start, length).toUtf8().size();
        zwp_input_method_context_v1_delete_surrounding_text(m_context, int(position - cursor),
                                                           uint32_t(bytes));
    }
    if (latest.known()) {
        if (m_expectedSurrounding.size() < size_t(maxPendingEdits)
            && latest.text.size() + text.size() <= maxSurroundingBytes) {
            SurroundingText expected = latest;
            expected.text.replace(start, length, text);
            expected.cursor = expected.anchor = start + text.size();
            m_expectedSurrounding.push_back(std::move(expected));
            m_contextNotification.stop();
            m_surroundingTimeout.start();
        } else {
            forgetSurrounding();
        }
    }
    zwp_input_method_context_v1_cursor_position(m_context, 0, 0);
    zwp_input_method_context_v1_commit_string(m_context, m_serial, text.toUtf8().constData());
    flush();
    return true;
}

void WaylandKeyboard::preedit(const QString &text)
{
    if (!active())
        return;
    const QByteArray utf8 = text.toUtf8();
    if (!utf8.isEmpty()) {
        zwp_input_method_context_v1_preedit_styling(m_context, 0, uint32_t(utf8.size()),
                                                  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_UNDERLINE);
        zwp_input_method_context_v1_preedit_cursor(m_context, int(utf8.size()));
    }
    zwp_input_method_context_v1_preedit_string(m_context, m_serial, utf8.constData(), "");
    flush();
}

void WaylandKeyboard::key(int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    if (!active())
        return;
    xkb_keysym_t symbol = XKB_KEY_NoSymbol;
    switch (key) {
    case Qt::Key_Backspace: symbol = XKB_KEY_BackSpace; break;
    case Qt::Key_Delete: symbol = XKB_KEY_Delete; break;
    case Qt::Key_Left: symbol = XKB_KEY_Left; break;
    case Qt::Key_Right: symbol = XKB_KEY_Right; break;
    case Qt::Key_Return: symbol = XKB_KEY_Return; break;
    case Qt::Key_Enter: symbol = XKB_KEY_KP_Enter; break;
    case Qt::Key_Tab: symbol = XKB_KEY_Tab; break;
    default:
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
            symbol = XKB_KEY_a + xkb_keysym_t(key - Qt::Key_A);
        else if (!text.isEmpty())
            symbol = xkb_utf32_to_keysym(text.toUcs4().first());
        break;
    }
    if (symbol == XKB_KEY_NoSymbol)
        return;
    if (key != Qt::Key_C || modifiers != Qt::ControlModifier)
        forgetSurrounding();
    uint32_t mask = 0;
    if (modifiers & Qt::ShiftModifier)
        mask |= 1;
    if (modifiers & Qt::ControlModifier)
        mask |= 2;
    if (modifiers & Qt::AltModifier)
        mask |= 4;
    zwp_input_method_context_v1_keysym(m_context, m_serial, 0, symbol, WL_KEYBOARD_KEY_STATE_PRESSED, mask);
    zwp_input_method_context_v1_keysym(m_context, m_serial, 0, symbol, WL_KEYBOARD_KEY_STATE_RELEASED, mask);
    flush();
}

void WaylandKeyboard::hidePanel()
{
    if (m_hidden || !m_context)
        return;
    preedit({});
    m_hidden = true;
    forgetSurrounding();
    cancelInput();
    emit sessionChanged();
    emit stateChanged();
    wl_surface_attach(m_surface, nullptr, 0, 0);
    wl_surface_commit(m_surface);
    flush();
}

void WaylandKeyboard::scheduleRender()
{
    m_renderDirty = true;
    wakeRenderer();
}

void WaylandKeyboard::wakeRenderer()
{
    const bool available = m_frames.size() < 2
        || std::any_of(m_frames.cbegin(), m_frames.cend(), [](const auto &frame) { return !frame->busy; });
    if (m_renderDirty && m_panel && m_surface && !m_hidden && available && !m_renderTimer.isActive())
        m_renderTimer.start(16);
}

void WaylandKeyboard::render()
{
    if (!m_renderDirty || m_hidden || !m_panel)
        return;

    m_renderDirty = false;
    const bool rotated = m_outputTransform == WL_OUTPUT_TRANSFORM_90
        || m_outputTransform == WL_OUTPUT_TRANSFORM_270
        || m_outputTransform == WL_OUTPUT_TRANSFORM_FLIPPED_90
        || m_outputTransform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
    const int transformedWidth = rotated ? m_outputMode.height() : m_outputMode.width();
    const int width = std::clamp(transformedWidth / m_outputScale, 1, 2048);
    m_panel->setWidth(width);
    const auto height = std::clamp(int(m_panel->implicitHeight()), 1, 720);
    m_panel->setHeight(height);
    const QSize size(width, height);
    if (m_image.size() != size) {
        cancelInput();
        m_image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        if (m_image.isNull())
            return;
        m_window.setGeometry(0, 0, width, height);
        m_window.setRenderTarget(QQuickRenderTarget::fromPaintDevice(&m_image));
    }
    m_frames.erase(std::remove_if(m_frames.begin(), m_frames.end(), [&size](const auto &frame) {
        return !frame->busy && frame->size != size;
    }), m_frames.end());
    const auto free = std::find_if(m_frames.begin(), m_frames.end(),
                                   [](const auto &frame) { return !frame->busy; });
    Frame *frame;
    if (free != m_frames.end()) {
        frame = free->get();
    } else {
        if (m_frames.size() >= 2) {
            m_renderDirty = true;
            return;
        }
        auto fresh = createFrame(size);
        if (!fresh)
            return;
        frame = fresh.get();
        m_frames.push_back(std::move(fresh));
    }
    m_renderControl.polishItems();
    m_renderControl.sync();
    m_renderControl.render();

    memcpy(frame->memory, m_image.constBits(), size_t(frame->bytes));
    frame->busy = true;
    wl_surface_attach(m_surface, frame->buffer, 0, 0);
    wl_surface_damage(m_surface, 0, 0, width, height);
    wl_surface_commit(m_surface);
    flush();
}

std::unique_ptr<WaylandKeyboard::Frame> WaylandKeyboard::createFrame(const QSize &size)
{
    auto frame = std::make_unique<Frame>();
    frame->owner = this;
    frame->size = size;
    frame->bytes = m_image.sizeInBytes();
    QTemporaryFile file;
    if (!file.open() || !file.resize(frame->bytes))
        return nullptr;
    frame->memory = mmap(nullptr, size_t(frame->bytes), PROT_READ | PROT_WRITE, MAP_SHARED,
                         file.handle(), 0);
    if (frame->memory == MAP_FAILED)
        return nullptr;
    wl_shm_pool *pool = wl_shm_create_pool(m_shm, file.handle(), int(frame->bytes));
    if (!pool)
        return nullptr;
    frame->buffer = wl_shm_pool_create_buffer(pool, 0, size.width(), size.height(), int(m_image.bytesPerLine()),
                                              WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!frame->buffer)
        return nullptr;
    static const wl_buffer_listener bufferListener{
        [](void *data, wl_buffer *) {
            auto *frame = static_cast<Frame *>(data);
            frame->owner->releaseFrame(frame);
        }
    };
    wl_buffer_add_listener(frame->buffer, &bufferListener, frame.get());
    return frame;
}

void WaylandKeyboard::releaseFrame(Frame *frame)
{
    frame->busy = false;
    wakeRenderer();
}

void WaylandKeyboard::cancelPointer()
{
    if (m_pressed)
        pointerEvent(QEvent::MouseButtonRelease, {-1, -1});
}

void WaylandKeyboard::cancelInput()
{
    cancelPointer();
    m_nativeTouch.cancel();
}

void WaylandKeyboard::pointerEvent(QEvent::Type type, QPointF position)
{
    m_pointerPosition = position;
    if (type == QEvent::MouseButtonPress)
        m_pressed = true;
    else if (type == QEvent::MouseButtonRelease)
        m_pressed = false;
    QMouseEvent event(type, position, position, position,
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      m_pressed ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&m_window, &event);
    scheduleRender();
}

void WaylandKeyboard::updateSeat(uint32_t capabilities)
{
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !m_pointer) {
        static const wl_pointer_listener pointerListener{
            [](void *data, wl_pointer *, uint32_t, wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                self->m_pointerInside = surface == self->m_surface;
                self->m_pointerPosition = {wl_fixed_to_double(x), wl_fixed_to_double(y)};
            },
            [](void *data, wl_pointer *, uint32_t, wl_surface *) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                self->m_pointerInside = false;
                self->cancelPointer();
            },
            [](void *data, wl_pointer *, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                if (self->m_pointerInside && !self->m_nativeTouch.hasContacts())
                    self->pointerEvent(QEvent::MouseMove, {wl_fixed_to_double(x), wl_fixed_to_double(y)});
            },
            [](void *data, wl_pointer *, uint32_t, uint32_t, uint32_t button, uint32_t state) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                if (self->m_pointerInside && !self->m_nativeTouch.hasContacts()
                    && button == BTN_LEFT && self->active())
                    self->pointerEvent(state == WL_POINTER_BUTTON_STATE_PRESSED
                                           ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                                       self->m_pointerPosition);
            },
            [](void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) {},
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
        };
        m_pointer = wl_seat_get_pointer(m_seat);
        wl_pointer_add_listener(m_pointer, &pointerListener, this);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && m_pointer) {
        cancelPointer();
        wl_pointer_destroy(m_pointer);
        m_pointer = nullptr;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !m_touch) {
        static const wl_touch_listener touchListener{
            [](void *data, wl_touch *, uint32_t, uint32_t, wl_surface *surface, int32_t id,
               wl_fixed_t x, wl_fixed_t y) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                if (surface == self->m_surface && !self->m_pressed && self->active())
                    self->m_nativeTouch.down(id, {wl_fixed_to_double(x), wl_fixed_to_double(y)});
            },
            [](void *data, wl_touch *, uint32_t, uint32_t, int32_t id) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                self->m_nativeTouch.up(id);
            },
            [](void *data, wl_touch *, uint32_t, int32_t id, wl_fixed_t x, wl_fixed_t y) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                self->m_nativeTouch.move(id, {wl_fixed_to_double(x), wl_fixed_to_double(y)});
            },
            [](void *data, wl_touch *) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                if (self->m_nativeTouch.dispatchFrame())
                    self->scheduleRender();
            },
            [](void *data, wl_touch *) {
                auto *self = static_cast<WaylandKeyboard *>(data);
                self->m_nativeTouch.cancel();
                self->scheduleRender();
            },
            nullptr, nullptr
        };
        m_touch = wl_seat_get_touch(m_seat);
        wl_touch_add_listener(m_touch, &touchListener, this);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && m_touch) {
        m_nativeTouch.cancel();
        wl_touch_destroy(m_touch);
        m_touch = nullptr;
    }
}
