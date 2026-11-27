#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QTimer>

#include <algorithm>

namespace {
QQuickItem *findItem(QQuickItem *item, const QString &name)
{
    if (item->isVisible() && item->objectName() == name)
        return item;
    for (auto *child : item->childItems()) {
        if (auto *result = findItem(child, name))
            return result;
    }
    return nullptr;
}

class Metrics final : public QObject
{
public:
    explicit Metrics(QQuickWindow *window) : m_window(window)
    {
        m_clock.start();
        window->installEventFilter(this);
        connect(window, &QQuickWindow::frameSwapped, this, [this] {
            const auto now = m_clock.nsecsElapsed();
            if (m_pendingPress > 0) {
                m_pressFrames.append(now - m_pendingPress);
                m_pendingPress = 0;
            }
            if (m_pendingCandidate > 0) {
                m_candidateFrames.append(now - m_pendingCandidate);
                m_pendingCandidate = 0;
            }
        });
    }

    void candidateVisible()
    {
        if (m_latestRelease > 0) {
            m_pendingCandidate = m_latestRelease;
            m_latestRelease = 0;
        }
    }

    void report() const
    {
        qInfo("touch begin=%d end=%d", m_begins, m_ends);
        reportSeries("touch-to-frame", m_pressFrames);
        reportSeries("release-to-candidate-frame", m_candidateFrames);
    }

    int releases() const { return m_ends; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_window) {
            if (event->type() == QEvent::TouchBegin) {
                ++m_begins;
                m_pendingPress = m_clock.nsecsElapsed();
            } else if (event->type() == QEvent::TouchEnd) {
                ++m_ends;
                m_latestRelease = m_clock.nsecsElapsed();
            }
        }
        return false;
    }

private:
    static void reportSeries(const char *name, QList<qint64> samples)
    {
        if (samples.isEmpty())
            return;
        std::sort(samples.begin(), samples.end());
        const auto rank = [&](qsizetype numerator) {
            const auto index = (numerator * samples.size() + 99) / 100 - 1;
            return double(samples.at(index)) / 1'000'000.0;
        };
        qInfo("%s: samples=%lld p50=%.2f ms p95=%.2f ms", name,
              static_cast<long long>(samples.size()), rank(50), rank(95));
    }

    QQuickWindow *m_window;
    QElapsedTimer m_clock;
    qint64 m_pendingPress = 0;
    qint64 m_latestRelease = 0;
    qint64 m_pendingCandidate = 0;
    int m_begins = 0;
    int m_ends = 0;
    QList<qint64> m_pressFrames;
    QList<qint64> m_candidateFrames;
};

class CandidateObserver final : public QObject
{
    Q_OBJECT

public:
    CandidateObserver(QObject *controller, Metrics &metrics)
        : m_controller(controller), m_metrics(metrics)
    {}

public slots:
    void candidatesChanged()
    {
        if (!m_controller->property("candidates").toList().isEmpty())
            m_metrics.candidateVisible();
    }

private:
    QObject *m_controller;
    Metrics &m_metrics;
};

int processPssKiB()
{
    QFile rollup(QStringLiteral("/proc/self/smaps_rollup"));
    if (!rollup.open(QIODevice::ReadOnly))
        return -1;
    const auto lines = rollup.readAll().split('\n');
    for (const auto &line : lines) {
        if (line.startsWith("Pss:"))
            return line.simplified().split(' ').value(1).toInt();
    }
    return -1;
}

void startSoak(QQuickWindow *window, QQuickItem *editor, QQuickItem *pin, QObject *controller, int seconds)
{
    auto *step = new QTimer(window);
    step->setInterval(125);
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(step, &QTimer::timeout, window,
                     [=, phase = 0, cycles = 0, lastReport = qint64{-60'000},
                      baseline = -1, maximum = -1, attempts = 0]() mutable {
        auto input = [controller](const QString &text) {
            return QMetaObject::invokeMethod(controller, "input", Q_ARG(QString, text));
        };
        auto fail = [step](const char *message) {
            step->stop();
            qCritical("Soak FAIL: %s", message);
            QCoreApplication::exit(1);
        };

        if (phase == 0) {
            window->setProperty("scenario", QStringLiteral("direct"));
            controller->setProperty("language", QStringLiteral("en-US"));
            editor->forceActiveFocus();
            editor->setProperty("text", QString{});
            phase = 1;
        } else if (phase == 1) {
            if (!input(QStringLiteral("q")) || !input(QStringLiteral("w"))
                || editor->property("text").toString() != u"qw")
                return fail("direct input");
            window->setProperty("scenario", QStringLiteral("pinyin"));
            controller->setProperty("language", QStringLiteral("zh-CN"));
            editor->setProperty("text", QString{});
            phase = 2;
        } else if (phase == 2) {
            for (const auto letter : QStringLiteral("nihao")) {
                if (!input(QString(letter)))
                    return fail("pinyin input");
            }
            attempts = 0;
            phase = 3;
        } else if (phase == 3) {
            const auto words = controller->property("candidates").toList();
            if (words.isEmpty()) {
                if (++attempts > 160) {
                    qInfo("Candidate state: language=%s active=%d sensitive=%d preedit length=%lld",
                          qPrintable(controller->property("language").toString()),
                          controller->property("active").toBool(), controller->property("sensitive").toBool(),
                          static_cast<long long>(controller->property("preedit").toString().size()));
                    return fail("pinyin candidates timed out");
                }
                return;
            }
            const auto first = words.first().toMap();
            const auto id = first.value(QStringLiteral("id")).toString();
            if (first.value(QStringLiteral("text")).toString() != u"你好" || id.isEmpty())
                return fail("pinyin first candidate");
            if (!QMetaObject::invokeMethod(controller, "selectCandidate", Q_ARG(QString, id))
                || editor->property("text").toString() != u"你好")
                return fail("pinyin commit");
            input(QStringLiteral("ni"));
            window->setProperty("scenario", QStringLiteral("pin"));
            pin->forceActiveFocus();
            pin->setProperty("text", QString{});
            phase = 4;
        } else {
            if (!input(QStringLiteral("x1")))
                return fail("PIN input");
            if (pin->property("text").toString() != u"1"
                || !controller->property("sensitive").toBool()
                || !controller->property("preedit").toString().isEmpty()
                || !controller->property("candidates").toList().isEmpty())
                return fail("PIN privacy or stale candidates");
            ++cycles;
            phase = 0;

            const auto now = elapsed.elapsed();
            if (now - lastReport >= 60'000 || now >= qint64(seconds) * 1000) {
                const int pss = processPssKiB();
                if (pss < 0)
                    return fail("could not measure process PSS");
                if (baseline < 0)
                    baseline = pss;
                maximum = std::max(maximum, pss);
                qInfo("SOAK seconds=%lld cycles=%d pss=%d KiB peak=%d KiB",
                      static_cast<long long>(now / 1000), cycles, pss, maximum);
                lastReport = now;
            }
            if (now >= qint64(seconds) * 1000) {
                step->stop();
                qInfo("SOAK PASS: cycles=%d baseline=%d KiB peak=%d KiB", cycles, baseline, maximum);
                QCoreApplication::quit();
            }
        }
    });
    step->start();
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VeloraIME"));
    QCoreApplication::setApplicationName(QStringLiteral("Touch Runtime Test"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("import"), QStringLiteral("Installed QML import root."));
    parser.addPositionalArgument(QStringLiteral("scenario"), QStringLiteral("direct, pinyin, pin, or soak."));
    parser.addPositionalArgument(QStringLiteral("expected"), QStringLiteral("Expected committed test text."));
    parser.addPositionalArgument(QStringLiteral("count"), QStringLiteral("Minimum touch releases, or soak seconds."));
    parser.process(app);
    const QStringList arguments = parser.positionalArguments();
    if (arguments.size() != 4)
        return 2;
    const QString scenario = arguments.at(1);
    bool countOk = false;
    const int minimumReleases = arguments.at(3).toInt(&countOk);
    const bool soak = scenario == u"soak";
    if ((scenario != u"direct" && scenario != u"pinyin" && scenario != u"pin" && !soak)
        || !countOk || minimumReleases <= 0)
        return 2;

    QQmlApplicationEngine engine;
    engine.addImportPath(arguments.at(0));
    engine.setInitialProperties({{QStringLiteral("scenario"), soak ? QStringLiteral("direct") : scenario}});
    engine.load(QUrl(QStringLiteral("qrc:/probe/Probe.qml")));
    auto *window = engine.rootObjects().isEmpty() ? nullptr
        : qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    if (!window)
        return 1;
    window->showFullScreen();
    const QString editorName = scenario == u"pin" ? QStringLiteral("probePin") : QStringLiteral("probeEditor");
    auto *editor = window->findChild<QQuickItem *>(editorName);
    auto *keyboard = window->findChild<QQuickItem *>(QStringLiteral("probeKeyboard"));
    auto *controller = keyboard ? keyboard->property("controller").value<QObject *>() : nullptr;
    if (!editor || !keyboard || !controller)
        return 1;
    if (soak) {
        auto *pin = window->findChild<QQuickItem *>(QStringLiteral("probePin"));
        if (!pin)
            return 1;
        QTimer::singleShot(500, window, [=] { startSoak(window, editor, pin, controller, minimumReleases); });
        return app.exec();
    }

    Metrics metrics(window);
    CandidateObserver candidates(controller, metrics);
    QObject::connect(controller, SIGNAL(candidatesChanged()), &candidates, SLOT(candidatesChanged()));
    QTimer::singleShot(1100, window, [window, editor, keyboard, controller, scenario] {
        editor->forceActiveFocus();
        qInfo("READY scenario=%s logical=%dx%d scale=%.4f", qPrintable(scenario), window->width(),
              window->height(), double(window->devicePixelRatio()));
        const QStringList keys = scenario == u"pin"
            ? QStringList{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}
            : QStringLiteral("abcdefghijklmnopqrstuvwxyz").split(QString{}, Qt::SkipEmptyParts);
        for (const auto &key : keys) {
            if (const auto *item = findItem(keyboard, QStringLiteral("key-text-") + key)) {
                const auto center = item->mapToScene({item->width() / 2, item->height() / 2})
                    * window->devicePixelRatio();
                qInfo("KEY %s %d %d", qPrintable(key), qRound(center.x()), qRound(center.y()));
            }
        }
        for (auto name : {"space", "backspace"}) {
            if (const auto *item = findItem(keyboard, QStringLiteral("key-") + QString::fromLatin1(name))) {
                const auto center = item->mapToScene({item->width() / 2, item->height() / 2})
                    * window->devicePixelRatio();
                qInfo("KEY %s %d %d", name, qRound(center.x()), qRound(center.y()));
            }
        }
        qInfo("FOCUS %d sensitive=%d", editor->hasActiveFocus(), controller->property("sensitive").toBool());
    });

    auto *poll = new QTimer(window);
    poll->setInterval(100);
    QObject::connect(poll, &QTimer::timeout, window, [&] {
        if (metrics.releases() >= minimumReleases
            && editor->property("text").toString() == arguments.at(2)) {
            metrics.report();
            const bool passed = editor->hasActiveFocus()
                && controller->property("sensitive").toBool() == (scenario == u"pin");
            qInfo("RESULT %s", passed ? "PASS" : "FAIL");
            QCoreApplication::exit(passed ? 0 : 1);
        }
    });
    poll->start();
    QTimer::singleShot(45'000, window, [&] {
        metrics.report();
        qInfo("RESULT FAIL text-size=%lld", static_cast<long long>(editor->property("text").toString().size()));
        QCoreApplication::exit(1);
    });
    return app.exec();
}

#include "main.moc"
