#include "handwritingrecognizer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJSValue>
#include <QLibrary>
#include <QtConcurrentRun>

#include <cmath>
#include <utility>

namespace Velora {
namespace {

struct ZinniaRecognizer;
struct ZinniaCharacter;
struct ZinniaResult;

constexpr int grid = 1000;
constexpr int inset = 70;
constexpr qsizetype maxStrokes = 64;
constexpr qsizetype maxPoints = 8192;
constexpr size_t maxCandidates = 10;

struct Recognition {
    QStringList candidates;
    bool invalidModel = false;
};

QVariant plainValue(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return value.value<QJSValue>().toVariant();
    return value;
}

std::optional<QPointF> readPoint(const QVariant &value, double width, double height)
{
    const auto pointValue = plainValue(value);
    QPointF point;
    if (pointValue.canConvert<QPointF>()) {
        point = pointValue.toPointF();
    } else {
        const auto object = pointValue.toMap();
        bool validX = false;
        bool validY = false;
        point.setX(object.value(QStringLiteral("x")).toDouble(&validX));
        point.setY(object.value(QStringLiteral("y")).toDouble(&validY));
        if (!validX || !validY)
            return std::nullopt;
    }
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
        return std::nullopt;
    return QPointF(qBound(0.0, point.x(), width), qBound(0.0, point.y(), height));
}

QList<QList<QPoint>> normalize(const QVariantList &input, double width, double height)
{
    if (input.isEmpty() || input.size() > maxStrokes
        || !std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1
        || width > 16384 || height > 16384) {
        return {};
    }

    QList<QList<QPointF>> strokes;
    double left = width;
    double right = 0;
    double top = height;
    double bottom = 0;
    qsizetype pointCount = 0;
    for (const auto &strokeValue : input) {
        const auto points = plainValue(strokeValue).toList();
        if (points.size() > maxPoints - pointCount)
            return {};
        pointCount += points.size();
        QList<QPointF> stroke;
        stroke.reserve(points.size());
        for (const auto &pointValue : points) {
            const auto point = readPoint(pointValue, width, height);
            if (!point)
                return {};
            if (!stroke.isEmpty() && stroke.constLast() == *point)
                continue;
            left = qMin(left, point->x());
            right = qMax(right, point->x());
            top = qMin(top, point->y());
            bottom = qMax(bottom, point->y());
            stroke.append(*point);
        }
        if (!stroke.isEmpty())
            strokes.append(std::move(stroke));
    }

    if (strokes.isEmpty())
        return {};

    const QPointF center((left + right) / 2, (top + bottom) / 2);
    const double extent = qMax(1.0, qMax(right - left, bottom - top));
    const double scale = (grid - 2 * inset) / extent;
    QList<QList<QPoint>> result;
    result.reserve(strokes.size());
    for (const auto &stroke : std::as_const(strokes)) {
        QList<QPoint> transformed;
        transformed.reserve(stroke.size());
        for (const auto &point : stroke) {
            const auto offset = (point - center) * scale;
            const QPoint coordinate(qRound(grid / 2.0 + offset.x()),
                                    qRound(grid / 2.0 + offset.y()));
            if (transformed.isEmpty() || transformed.constLast() != coordinate)
                transformed.append(coordinate);
        }
        result.append(std::move(transformed));
    }
    return result;
}

template <typename Function>
Function resolveSymbol(const QString &path, const char *symbol)
{
    const auto pointer = path.isEmpty() ? QLibrary::resolve(QStringLiteral("zinnia"), 0, symbol)
                                       : QLibrary::resolve(path, symbol);
    return reinterpret_cast<Function>(pointer);
}

} // namespace

struct HandwritingRecognizer::Backend {
    ZinniaRecognizer *(*newRecognizer)() = nullptr;
    void (*destroyRecognizer)(ZinniaRecognizer *) = nullptr;
    int (*openRecognizer)(ZinniaRecognizer *, const char *) = nullptr;
    ZinniaCharacter *(*newCharacter)() = nullptr;
    void (*destroyCharacter)(ZinniaCharacter *) = nullptr;
    void (*setWidth)(ZinniaCharacter *, size_t) = nullptr;
    void (*setHeight)(ZinniaCharacter *, size_t) = nullptr;
    int (*addPoint)(ZinniaCharacter *, size_t, int, int) = nullptr;
    ZinniaResult *(*classify)(ZinniaRecognizer *, const ZinniaCharacter *, size_t) = nullptr;
    const char *(*candidateValue)(ZinniaResult *, size_t) = nullptr;
    size_t (*candidateCount)(ZinniaResult *) = nullptr;
    void (*destroyResult)(ZinniaResult *) = nullptr;
    ZinniaRecognizer *recognizer = nullptr;
    QString modelPath;

    Backend()
    {
        modelPath = qEnvironmentVariable("VELORA_HANDWRITING_MODEL",
            QStringLiteral("/usr/share/tegaki/models/zinnia/handwriting-zh_CN.model"));
        const QFileInfo model(modelPath);
        const auto library = qEnvironmentVariable("VELORA_ZINNIA_LIBRARY");
        if (!model.isFile() || !model.isReadable()
            || (!library.isEmpty() && !QDir::isAbsolutePath(library))) {
            return;
        }

        newRecognizer = resolveSymbol<decltype(newRecognizer)>(library, "zinnia_recognizer_new");
        destroyRecognizer = resolveSymbol<decltype(destroyRecognizer)>(library, "zinnia_recognizer_destroy");
        openRecognizer = resolveSymbol<decltype(openRecognizer)>(library, "zinnia_recognizer_open");
        newCharacter = resolveSymbol<decltype(newCharacter)>(library, "zinnia_character_new");
        destroyCharacter = resolveSymbol<decltype(destroyCharacter)>(library, "zinnia_character_destroy");
        setWidth = resolveSymbol<decltype(setWidth)>(library, "zinnia_character_set_width");
        setHeight = resolveSymbol<decltype(setHeight)>(library, "zinnia_character_set_height");
        addPoint = resolveSymbol<decltype(addPoint)>(library, "zinnia_character_add");
        classify = resolveSymbol<decltype(classify)>(library, "zinnia_recognizer_classify");
        candidateValue = resolveSymbol<decltype(candidateValue)>(library, "zinnia_result_value");
        candidateCount = resolveSymbol<decltype(candidateCount)>(library, "zinnia_result_size");
        destroyResult = resolveSymbol<decltype(destroyResult)>(library, "zinnia_result_destroy");
    }

    ~Backend()
    {
        if (recognizer)
            destroyRecognizer(recognizer);
    }

    bool ready() const
    {
        return newRecognizer && destroyRecognizer && openRecognizer && newCharacter
            && destroyCharacter && setWidth && setHeight && addPoint && classify
            && candidateValue && candidateCount && destroyResult;
    }

    Recognition recognize(const Strokes &strokes)
    {
        if (!recognizer) {
            recognizer = newRecognizer();
            const auto path = QFile::encodeName(modelPath);
            if (!recognizer || !openRecognizer(recognizer, path.constData())) {
                if (recognizer)
                    destroyRecognizer(std::exchange(recognizer, nullptr));
                return {{}, true};
            }
        }

        const std::unique_ptr<ZinniaCharacter, decltype(destroyCharacter)>
            character(newCharacter(), destroyCharacter);
        if (!character)
            return {};
        setWidth(character.get(), grid);
        setHeight(character.get(), grid);
        for (qsizetype stroke = 0; stroke < strokes.size(); ++stroke) {
            for (const auto &point : strokes.at(stroke)) {
                if (!addPoint(character.get(), static_cast<size_t>(stroke), point.x(), point.y()))
                    return {};
            }
        }

        const std::unique_ptr<ZinniaResult, decltype(destroyResult)>
            result(classify(recognizer, character.get(), maxCandidates), destroyResult);
        if (!result)
            return {};
        Recognition output;
        const auto count = qMin(candidateCount(result.get()), maxCandidates);
        for (size_t index = 0; index < count; ++index) {
            const auto value = QString::fromUtf8(candidateValue(result.get(), index));
            if (!value.isEmpty() && !output.candidates.contains(value))
                output.candidates.append(value);
        }
        return output;
    }
};

HandwritingRecognizer::HandwritingRecognizer(QObject *parent)
    : QObject(parent), m_backend(std::make_shared<Backend>()), m_available(m_backend->ready())
{
}

HandwritingRecognizer::~HandwritingRecognizer() = default;

void HandwritingRecognizer::request(const QVariantList &strokes, double width, double height)
{
    if (!m_available)
        return;

    auto input = normalize(strokes, width, height);
    if (input.isEmpty()) {
        cancel();
        return;
    }

    ++m_generation;
    m_pending = std::move(input);
    setCandidates({});
    startNext();
    setBusy(m_running || m_pending.has_value());
}

void HandwritingRecognizer::cancel()
{
    ++m_generation;
    m_pending.reset();
    setCandidates({});
    setBusy(false);
}

void HandwritingRecognizer::startNext()
{
    if (m_running || !m_pending)
        return;

    m_running = true;
    auto input = std::move(*m_pending);
    m_pending.reset();
    const quint64 generation = m_generation;
    auto *watcher = new QFutureWatcher<Recognition>(this);
    connect(watcher, &QFutureWatcher<Recognition>::finished, this, [this, watcher, generation] {
        auto result = watcher->result();
        watcher->deleteLater();
        m_running = false;
        if (result.invalidModel) {
            m_pending.reset();
            m_available = false;
            setCandidates({});
            emit availableChanged();
        } else if (generation == m_generation) {
            setCandidates(std::move(result.candidates));
        }
        startNext();
        setBusy(m_running || m_pending.has_value());
    });
    watcher->setFuture(QtConcurrent::run([backend = m_backend, input = std::move(input)] {
        return backend->recognize(input);
    }));
}

void HandwritingRecognizer::setCandidates(QStringList candidates)
{
    if (m_candidates == candidates)
        return;
    m_candidates = std::move(candidates);
    emit candidatesChanged();
}

void HandwritingRecognizer::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

} // namespace Velora
