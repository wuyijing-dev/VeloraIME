#include <QCommandLineParser>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {
qint64 monotonicNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

const QQuickItem *visibleItem(const QQuickItem *parent, const QString &name)
{
    if (!parent->isVisible())
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (const auto *child : parent->childItems()) {
        if (const auto *result = visibleItem(child, name))
            return result;
    }
    return nullptr;
}

bool hasHitArea(const QQuickItem *key)
{
    return key && key->isEnabled() && key->width() > 0 && key->height() > 0;
}

void tap(QQuickWindow &window, QPointingDevice *device, const QQuickItem &item)
{
    const auto center = item.mapToScene(QPointF(item.width() / 2, item.height() / 2)).toPoint();
    QTest::touchEvent(&window, device).press(0, center, &window);
    QTest::touchEvent(&window, device).release(0, center, &window);
}

class Probe final : public QObject
{
public:
    Probe(QQuickWindow &window, QQuickItem &editor, QQuickItem &panel)
        : m_window(window), m_editor(editor), m_panel(panel), m_touch(QTest::createTouchDevice())
    {
        m_window.installEventFilter(this);
        connect(&window, &QQuickWindow::afterSynchronizing, this, [this] {
            const auto touchEndNs = m_touchEndNs.load();
            m_frameRequestNs = 0;
            if (touchEndNs && !m_completedNs.load() && m_editor.hasActiveFocus()
                && hasHitArea(visibleItem(&m_panel, u"key-text-q"_s)))
                m_frameRequestNs = touchEndNs;
        }, Qt::DirectConnection);
        connect(&window, &QQuickWindow::frameSwapped, this, [this] {
            if (m_frameRequestNs && m_frameRequestNs == m_touchEndNs.load() && !m_completedNs.load())
                m_completedNs.store(monotonicNs());
            m_swaps.fetch_add(1);
        }, Qt::DirectConnection);
    }

    ~Probe() override
    {
        m_window.removeEventFilter(this);
        // Drain the render thread while the signal receivers still exist.
        m_window.hide();
        m_window.releaseResources();
    }

    bool sample(double &milliseconds)
    {
        const auto *close = visibleItem(&m_panel, u"hideKeyboard"_s);
        if (!hasHitArea(close))
            return fail("hide key unavailable");
        const auto previousSwap = m_swaps.load();
        tap(m_window, m_touch, *close);
        if (!QTest::qWaitFor([this, previousSwap] {
                return !m_panel.isVisible() && m_swaps.load() > previousSwap;
            }, 1s))
            return fail("hidden keyboard frame unavailable");

        m_completedNs.store(0);
        m_armed = true;
        tap(m_window, m_touch, m_editor);
        if (!QTest::qWaitFor([this] { return m_completedNs.load() != 0; }, 1s))
            return fail("open keyboard frame unavailable");
        const auto timestamp = m_touchEndNs.exchange(0);
        milliseconds = (m_completedNs.load() - timestamp) / 1.0e6;
        if (timestamp == 0 || milliseconds < 0)
            return fail("touch release timestamp unavailable");

        const auto *letter = visibleItem(&m_panel, u"key-text-q"_s);
        if (!hasHitArea(letter))
            return fail("first key unavailable");
        tap(m_window, m_touch, *letter);
        if (m_editor.property("text").toString() != u"q"_s || !m_editor.hasActiveFocus())
            return fail("first key did not reach the editor");
        m_editor.setProperty("text", QString());
        return true;
    }

protected:
    bool eventFilter(QObject *subject, QEvent *event) override
    {
        if (m_armed && event->type() == QEvent::TouchEnd) {
            m_touchEndNs.store(monotonicNs());
            m_armed = false;
        }
        return QObject::eventFilter(subject, event);
    }

private:
    bool fail(const char *reason)
    {
        m_touchEndNs.store(0);
        m_armed = false;
        qCritical("Keyboard open probe: %s", reason);
        return false;
    }

    QQuickWindow &m_window;
    QQuickItem &m_editor;
    QQuickItem &m_panel;
    QPointingDevice *m_touch;
    std::atomic<qint64> m_touchEndNs{0};
    std::atomic<qint64> m_completedNs{0};
    std::atomic<quint64> m_swaps{0};
    qint64 m_frameRequestNs = 0;
    bool m_armed = false;
};

double percentile(const std::vector<double> &samples, size_t rank)
{
    return samples.at((samples.size() * rank + 99) / 100 - 1);
}

struct FrameTrace {
    explicit FrameTrace(int count) : timestamps(count) {}

    std::vector<qint64> timestamps;
    std::atomic<size_t> recorded{0};
};

bool reportFrames(QQuickWindow &window, int count, const char *phase, const QQuickItem *expand = nullptr)
{
    const auto trace = std::make_shared<FrameTrace>(count + 1);
    const auto connection = QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [trace] {
        const auto current = trace->recorded.load(std::memory_order_relaxed);
        if (current < trace->timestamps.size()) {
            trace->timestamps[current] = monotonicNs();
            trace->recorded.store(current + 1, std::memory_order_release);
        }
    }, Qt::DirectConnection);
    if (expand) {
        if (!QTest::qWaitFor([trace] { return trace->recorded.load() > 0; }, 1s)) {
            QObject::disconnect(connection);
            qCritical("No frame was available before expanding candidates");
            return false;
        }
        tap(window, QTest::createTouchDevice(), *expand);
    }

    const auto budget = std::chrono::milliseconds(2000 + 100 * count);
    const bool complete = QTest::qWaitFor([trace] {
        return trace->recorded.load(std::memory_order_acquire) == trace->timestamps.size();
    }, budget);
    QObject::disconnect(connection);
    if (!complete) {
        qCritical("Frame probe %s captured %zu of %zu timestamps", phase, trace->recorded.load(),
                  trace->timestamps.size());
        return false;
    }

    std::vector<double> intervals;
    intervals.reserve(count);
    for (size_t i = 1; i < trace->timestamps.size(); ++i)
        intervals.push_back((trace->timestamps[i] - trace->timestamps[i - 1]) / 1.0e6);
    std::sort(intervals.begin(), intervals.end());
    const auto overBudget = std::count_if(intervals.cbegin(), intervals.cend(), [](double interval) {
        return interval > 20.0;
    });
    qInfo("Frame probe %s n=%zu p50_ms=%.2f p95_ms=%.2f max_ms=%.2f over_20_ms=%lld",
          phase, intervals.size(), percentile(intervals, 50), percentile(intervals, 95), intervals.back(),
          static_cast<long long>(overBudget));
    return true;
}

bool frameProbe(QQuickWindow &window, QQuickItem &editor, QQuickItem &panel, QObject &controller,
                int count, const QString &graphics)
{
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        qCritical("The frame probe needs a QML shader-capable graphics API");
        return false;
    }
    if (!QMetaObject::invokeMethod(&controller, "input", Q_ARG(QString, u"nihao"_s))
        || !QTest::qWaitFor([&panel] {
            return hasHitArea(visibleItem(&panel, u"keyboardExpandCandidates"_s));
        }, 20s)) {
        qCritical("The frame probe did not obtain Chinese candidates");
        return false;
    }
    QTest::qWait(300);
    qInfo("Frame probe graphics=%s Qt %s QPA %s API %d logical %dx%d DPR %.3f",
          qPrintable(graphics), qVersion(), qPrintable(QGuiApplication::platformName()),
          int(window.rendererInterface()->graphicsApi()), window.width(), window.height(), window.devicePixelRatio());
    const auto *expand = visibleItem(&panel, u"keyboardExpandCandidates"_s);
    if (!hasHitArea(expand) || !reportFrames(window, count, "collapsed")
        || !reportFrames(window, count, "expand-and-expanded", expand))
        return false;
    if (visibleItem(&panel, u"key-text-q"_s) || !editor.hasActiveFocus()) {
        qCritical("The frame probe did not expand candidates with the editor still focused");
        return false;
    }
    return true;
}
}

int main(int argc, char **argv)
{
    QTemporaryDir scratch;
    if (!scratch.isValid())
        return 1;
    qputenv("XDG_DATA_HOME", QFile::encodeName(QDir(scratch.path()).filePath(u"data"_s)));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(QDir(scratch.path()).filePath(u"config"_s)));
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"VeloraIME Tests"_s);
    QCoreApplication::setApplicationName(u"Keyboard Open Probe"_s);
    QQuickStyle::setStyle(u"Basic"_s);

    QCommandLineParser arguments;
    arguments.addHelpOption();
    const QCommandLineOption imports(u"imports"_s, u"Installed QML import root."_s, u"path"_s);
    const QCommandLineOption count(u"samples"_s, u"Warm keyboard reopen samples."_s, u"count"_s, u"200"_s);
    const QCommandLineOption frames(u"frames"_s, u"Capture this many frame intervals per animated phase."_s,
                                    u"count"_s);
    const QCommandLineOption graphics(u"graphics"_s, u"Animated probe glass mode, full or light."_s,
                                      u"mode"_s, u"full"_s);
    const QCommandLineOption fullscreen(u"fullscreen"_s, u"Use the full current output."_s);
    arguments.addOptions({imports, count, frames, graphics, fullscreen});
    arguments.process(app);
    const auto runFrames = arguments.isSet(frames);
    const QString graphicsMode = arguments.value(graphics);
    bool valid = false;
    const auto repetitions = arguments.value(runFrames ? frames : count).toInt(&valid);
    const QDir prefix(arguments.value(imports));
    if (!arguments.isSet(imports) || !prefix.exists(u"Rk/TouchKeyboard/qmldir"_s) || !valid
        || repetitions < 1 || repetitions > 5000 || (runFrames && repetitions < 2)
        || (graphicsMode != u"full" && graphicsMode != u"light")) {
        qCritical("Supply an --imports directory and 1–5000 --samples or 2–5000 --frames, with full or light graphics");
        return 2;
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(prefix.absolutePath());
    if (runFrames)
        engine.setInitialProperties({{u"diagnosticGraphicsMode"_s, graphicsMode == u"full" ? 1 : 2}});
    engine.load(QUrl(runFrames ? u"qrc:/probe/FrameDiagnostic.qml"_s : u"qrc:/example/Main.qml"_s));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window)
        return 1;
    if (arguments.isSet(fullscreen))
        window->showFullScreen();
    else
        window->resize(414, 736);
    window->requestActivate();
    if (!QTest::qWaitForWindowExposed(window, 3s)) {
        qCritical("Probe window was not exposed");
        return 1;
    }
    auto *editor = window->findChild<QQuickItem *>(u"hostNote"_s);
    auto *panel = window->findChild<QQuickItem *>(u"hostKeyboard"_s);
    if (!editor || !panel)
        return 1;
    editor->forceActiveFocus();
    auto *controller = panel->property("controller").value<QObject *>();
    if (!controller || !QTest::qWaitFor([controller] { return controller->property("active").toBool(); }, 2s)) {
        qCritical("Probe editor did not acquire input focus");
        return 1;
    }

    if (runFrames)
        return frameProbe(*window, *editor, *panel, *controller, repetitions, graphicsMode) ? 0 : 1;

    Probe probe(*window, *editor, *panel);
    double warmup = 0;
    for (int i = 0; i < 8; ++i) {
        if (!probe.sample(warmup))
            return 1;
    }
    std::vector<double> samples;
    samples.reserve(repetitions);
    for (int i = 0; i < repetitions; ++i) {
        double latency = 0;
        if (!probe.sample(latency)) {
            qCritical("Sample %d of %d failed", i + 1, repetitions);
            return 1;
        }
        samples.push_back(latency);
    }
    std::sort(samples.begin(), samples.end());
    qInfo("Keyboard open Qt %s QPA %s graphics API %d logical %dx%d DPR %.3f",
          qVersion(), qPrintable(QGuiApplication::platformName()),
          int(window->rendererInterface()->graphicsApi()), window->width(), window->height(), window->devicePixelRatio());
    qInfo("Keyboard open n=%zu p50_ms=%.2f p95_ms=%.2f max_ms=%.2f",
          samples.size(), percentile(samples, 50), percentile(samples, 95), samples.back());
    return 0;
}
