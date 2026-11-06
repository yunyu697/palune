#include "mpv/MpvClient.h"

#include "diag/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

extern "C" {
#include <mpv/client.h>
}

namespace palune {

struct MpvClient::Api {
    mpv_handle *(*create)() = nullptr;
    int (*initialize)(mpv_handle *) = nullptr;
    int (*setOptionString)(mpv_handle *, const char *, const char *) = nullptr;
    int (*setOption)(mpv_handle *, const char *, mpv_format, void *) = nullptr;
    int (*setProperty)(mpv_handle *, const char *, mpv_format, void *) = nullptr;
    int (*setPropertyString)(mpv_handle *, const char *, const char *) = nullptr;
    int (*getProperty)(mpv_handle *, const char *, mpv_format, void *) = nullptr;
    char *(*getPropertyString)(mpv_handle *, const char *) = nullptr;
    int (*command)(mpv_handle *, const char **) = nullptr;
    int (*commandRet)(mpv_handle *, const char **, mpv_node *) = nullptr;
    mpv_event *(*waitEvent)(mpv_handle *, double) = nullptr;
    void (*terminateDestroy)(mpv_handle *) = nullptr;
    const char *(*errorString)(int) = nullptr;
    void (*freeString)(void *) = nullptr;
    void (*freeNodeContents)(mpv_node *) = nullptr;
    int (*requestLogMessages)(mpv_handle *, const char *) = nullptr;
};

template <typename T>
bool MpvClient::resolve(T &fn, const char *name)
{
    fn = reinterpret_cast<T>(m_library->resolve(name));
    if (!fn) {
        m_loadError = QStringLiteral("libmpv 缺少导出符号：%1").arg(name);
        return false;
    }
    return true;
}

MpvClient::MpvClient(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(5);
    connect(&m_timer, &QTimer::timeout, this, &MpvClient::poll);
}

MpvClient::~MpvClient()
{
    m_timer.stop();
    if (m_api && m_mpv) {
        m_api->terminateDestroy(m_mpv);
        m_mpv = nullptr;
    }
    delete m_api;
    m_api = nullptr;
    delete m_library;
    m_library = nullptr;
}

bool MpvClient::loadLibrary()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList names;
    names << appDir + QStringLiteral("/libmpv-2.dll")
          << QStringLiteral("libmpv-2")
          << QStringLiteral("libmpv.so.2")
          << QStringLiteral("libmpv.2.dylib")
          << QStringLiteral("libmpv.so");

    for (const QString &name : names) {
        auto *lib = new QLibrary(name);
        if (lib->load()) {
            m_library = lib;
            Log::info(QStringLiteral("libmpv 已动态加载：%1").arg(lib->fileName()));
            return true;
        }
        Log::warn(QStringLiteral("libmpv 加载失败：%1（%2）").arg(name, lib->errorString()));
        delete lib;
    }
    m_loadError = QStringLiteral("未找到 libmpv 动态库（libmpv-2.dll / libmpv.so.2 / libmpv.2.dylib）。"
                                 "禁止静态链接，请把动态库放在程序目录。");
    return false;
}

bool MpvClient::resolveSymbols()
{
    m_api = new Api;
    bool ok = true;
    ok &= resolve(m_api->create, "mpv_create");
    ok &= resolve(m_api->initialize, "mpv_initialize");
    ok &= resolve(m_api->setOptionString, "mpv_set_option_string");
    ok &= resolve(m_api->setOption, "mpv_set_option");
    ok &= resolve(m_api->setProperty, "mpv_set_property");
    ok &= resolve(m_api->setPropertyString, "mpv_set_property_string");
    ok &= resolve(m_api->getProperty, "mpv_get_property");
    ok &= resolve(m_api->getPropertyString, "mpv_get_property_string");
    ok &= resolve(m_api->command, "mpv_command");
    ok &= resolve(m_api->commandRet, "mpv_command_ret");
    ok &= resolve(m_api->waitEvent, "mpv_wait_event");
    ok &= resolve(m_api->terminateDestroy, "mpv_terminate_destroy");
    ok &= resolve(m_api->errorString, "mpv_error_string");
    ok &= resolve(m_api->freeString, "mpv_free");
    ok &= resolve(m_api->freeNodeContents, "mpv_free_node_contents");
    ok &= resolve(m_api->requestLogMessages, "mpv_request_log_messages");
    return ok;
}

bool MpvClient::initialize()
{
    if (!loadLibrary())
        return false;
    if (!resolveSymbols())
        return false;
    m_available = true;
    return true;
}

bool MpvClient::setVideoWindow(qintptr wid)
{
    if (!m_available)
        return false;
    if (!m_mpv) {
        m_mpv = m_api->create();
        if (!m_mpv) {
            m_loadError = QStringLiteral("mpv_create 失败");
            m_available = false;
            return false;
        }
        applyOptions(wid);
        Log::info(QStringLiteral("mpv 视频窗句柄：wid=%1").arg(wid));
        const int rc = m_api->initialize(m_mpv);
        if (rc < 0) {
            m_loadError = QStringLiteral("mpv_initialize 失败：%1").arg(m_api->errorString(rc));
            m_api->terminateDestroy(m_mpv);
            m_mpv = nullptr;
            m_available = false;
            return false;
        }
        m_api->requestLogMessages(m_mpv, qEnvironmentVariableIsSet("PALUNE_MPV_VERBOSE") ? "v" : "warn");
        m_initialized = true;
        m_timer.start();
        Log::info(QStringLiteral("mpv 已初始化，wid=%1").arg(wid));
    }
    return true;
}

void MpvClient::applyOptions(qintptr wid)
{
    auto opt = [this](const char *k, const char *v) {
        if (m_api->setOptionString(m_mpv, k, v) >= 0)
            Log::debug(QStringLiteral("mpv 选项 %1=%2").arg(k, v));
        else
            Log::warn(QStringLiteral("mpv 选项设置失败：%1=%2").arg(k, v));
    };

    const QByteArray voEnv = qEnvironmentVariable("PALUNE_MPV_VO").toUtf8();
    opt("vo", voEnv.isEmpty() ? "gpu-next" : voEnv.constData());
#if defined(Q_OS_WIN)
    const QByteArray ctxEnv = qEnvironmentVariable("PALUNE_MPV_GPU_CONTEXT").toUtf8();
    opt("gpu-context", ctxEnv.isEmpty() ? "d3d11" : ctxEnv.constData());
    const QByteArray hwdecEnv = qEnvironmentVariable("PALUNE_MPV_HWDEC").toUtf8();
    opt("hwdec", hwdecEnv.isEmpty() ? "d3d11va" : hwdecEnv.constData());
#elif defined(Q_OS_MACOS)
    opt("hwdec", "videotoolbox");
    opt("gpu-api", "opengl");
#else
    opt("hwdec", "auto-safe");
#endif
    opt("keep-open", "yes");
    opt("idle", "yes");
    opt("osc", "no");
    opt("input-default", "no");
    opt("input-vo-keyboard", "no");
    opt("audio-display", "no");
    opt("cursor-autohide", "no");
    opt("terminal", "no");
    if (qEnvironmentVariableIsSet("PALUNE_MPV_VERBOSE"))
        opt("msg-level", "all=v");
    opt("screenshot-high-bit-depth", "yes");

    if (wid != 0 && !qEnvironmentVariableIsSet("PALUNE_MPV_NO_WID")) {
        int64_t handle = static_cast<int64_t>(wid);
        if (m_api->setOption(m_mpv, "wid", MPV_FORMAT_INT64, &handle) < 0)
            Log::error(QStringLiteral("mpv wid 设置失败"));
    }
}

bool MpvClient::openTestPattern()
{
    if (!m_initialized)
        return false;
    const char *args[] = {"loadfile", "av://lavfi:testsrc=size=1280x720:rate=30", "replace", nullptr};
    const int rc = m_api->command(m_mpv, args);
    if (rc < 0) {
        emit errorOccurred(QStringLiteral("测试图样打开失败：%1").arg(m_api->errorString(rc)));
        return false;
    }
    m_filePath = QStringLiteral("lavfi:testsrc");
    emit filePathChanged();
    Log::info(QStringLiteral("已加载 lavfi 测试图样用于 M0 自测"));
    return true;
}

QString MpvClient::mpvVersion()
{
    if (!m_initialized)
        return QStringLiteral("未初始化");
    char *version = m_api->getPropertyString(m_mpv, "mpv-version");
    if (!version)
        return QStringLiteral("未知");
    const QString text = QString::fromUtf8(version);
    m_api->freeString(version);
    return text;
}

QString MpvClient::backendDescription() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("wid + d3d11 + gpu-next（Windows 主路径）");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("render API (GL) — 接口已预留，M0 未在本机验证");
#else
    return QStringLiteral("render API (GL) — 接口已预留，M0 未在本机验证");
#endif
}

bool MpvClient::openFile(const QString &path)
{
    if (!m_initialized) {
        m_loadError = QStringLiteral("mpv 尚未初始化");
        return false;
    }
    QFileInfo fi(path);
    const QString absolute = fi.absoluteFilePath();
    const char *args[] = {"loadfile", absolute.toUtf8().constData(), "replace", nullptr};
    const int rc = m_api->command(m_mpv, args);
    if (rc < 0) {
        emit errorOccurred(QStringLiteral("打开文件失败：%1").arg(m_api->errorString(rc)));
        return false;
    }
    m_filePath = absolute;
    emit filePathChanged();
    return true;
}

void MpvClient::togglePause()
{
    setPaused(!m_paused);
}

void MpvClient::setPaused(bool paused)
{
    if (!m_initialized)
        return;
    int flag = paused ? 1 : 0;
    if (m_api->setProperty(m_mpv, "pause", MPV_FORMAT_FLAG, &flag) >= 0) {
        m_paused = paused;
        emit pausedChanged();
    }
}

void MpvClient::seek(double seconds)
{
    if (!m_initialized)
        return;
    const QByteArray value = QByteArray::number(seconds, 'f', 3);
    const char *args[] = {"seek", value.constData(), "absolute+exact", nullptr};
    if (m_api->command(m_mpv, args) < 0)
        Log::warn(QStringLiteral("seek 失败：%1").arg(seconds));
}

void MpvClient::setVolume(double volume)
{
    if (!m_initialized)
        return;
    const double v = qBound(0.0, volume, 100.0);
    if (m_api->setProperty(m_mpv, "volume", MPV_FORMAT_DOUBLE, const_cast<double *>(&v)) >= 0) {
        m_volume = v;
        emit volumeChanged();
    }
}

void MpvClient::toggleMute()
{
    if (!m_initialized)
        return;
    int flag = m_muted ? 0 : 1;
    if (m_api->setProperty(m_mpv, "mute", MPV_FORMAT_FLAG, &flag) >= 0) {
        m_muted = flag != 0;
        emit mutedChanged();
    }
}

void MpvClient::stop()
{
    if (!m_initialized)
        return;
    const char *args[] = {"stop", nullptr};
    m_api->command(m_mpv, args);
}

void MpvClient::poll()
{
    if (!m_initialized)
        return;
    handleEvents();
    updateProperties();
}

void MpvClient::handleEvents()
{
    while (true) {
        mpv_event *event = m_api->waitEvent(m_mpv, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_FILE_LOADED:
            m_pendingFile = false;
            emit fileLoaded();
            break;
        case MPV_EVENT_END_FILE: {
            auto *end = static_cast<mpv_event_end_file *>(event->data);
            m_paused = true;
            emit pausedChanged();
            if (end && end->reason == MPV_END_FILE_REASON_ERROR) {
                const QString msg = QStringLiteral("播放失败：%1")
                                        .arg(m_api->errorString(end->error));
                emit errorOccurred(msg);
                emit endFile(msg);
            } else {
                emit endFile(QStringLiteral("eof"));
            }
            break;
        }
        case MPV_EVENT_VIDEO_RECONFIG:
            ++m_videoReconfigCount;
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            auto *log = static_cast<mpv_event_log_message *>(event->data);
            if (log && log->text) {
                const QString text = QString::fromUtf8(log->text).trimmed();
                if (log->log_level <= MPV_LOG_LEVEL_ERROR)
                    Log::error(QStringLiteral("mpv: %1").arg(text));
                else
                    Log::warn(QStringLiteral("mpv: %1").arg(text));
            }
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            m_timer.stop();
            break;
        default:
            break;
        }
    }
}

void MpvClient::updateProperties()
{
    double d = 0;
    if (m_api->getProperty(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &d) >= 0) {
        if (!qFuzzyCompare(d + 1.0, m_position + 1.0)) {
            m_position = d;
            emit positionChanged();
        }
    }
    if (m_api->getProperty(m_mpv, "duration", MPV_FORMAT_DOUBLE, &d) >= 0) {
        if (!qFuzzyCompare(d + 1.0, m_duration + 1.0)) {
            m_duration = d;
            emit durationChanged();
        }
    }
    int flag = 0;
    if (m_api->getProperty(m_mpv, "pause", MPV_FORMAT_FLAG, &flag) >= 0) {
        const bool p = flag != 0;
        if (p != m_paused) {
            m_paused = p;
            emit pausedChanged();
        }
    }
    if (m_api->getProperty(m_mpv, "volume", MPV_FORMAT_DOUBLE, &d) >= 0) {
        if (!qFuzzyCompare(d + 1.0, m_volume + 1.0)) {
            m_volume = d;
            emit volumeChanged();
        }
    }
    if (m_api->getProperty(m_mpv, "mute", MPV_FORMAT_FLAG, &flag) >= 0) {
        const bool muted = flag != 0;
        if (muted != m_muted) {
            m_muted = muted;
            emit mutedChanged();
        }
    }
    if (m_api->getProperty(m_mpv, "eof-reached", MPV_FORMAT_FLAG, &flag) >= 0)
        m_eofReached = flag != 0;

    int64_t i64 = 0;
    if (m_api->getProperty(m_mpv, "frame-drop-count", MPV_FORMAT_INT64, &i64) >= 0)
        m_frameDropCount = i64;
    if (m_api->getProperty(m_mpv, "mistimed-frame-count", MPV_FORMAT_INT64, &i64) >= 0)
        m_mistimedFrameCount = i64;

    char *title = m_api->getPropertyString(m_mpv, "media-title");
    if (title) {
        const QString t = QString::fromUtf8(title);
        m_api->freeString(title);
        if (t != m_title) {
            m_title = t;
            emit titleChanged();
        }
    }
}

bool MpvClient::sampleVideoLuminance(double *mean, int *width, int *height, QString *error)
{
    if (!m_initialized) {
        if (error) *error = QStringLiteral("mpv 未初始化");
        return false;
    }
    const char *args[] = {"screenshot-raw", "video", nullptr};
    mpv_node result;
    memset(&result, 0, sizeof(result));
    const int rc = m_api->commandRet(m_mpv, args, &result);
    if (rc < 0 || result.format != MPV_FORMAT_NODE_MAP) {
        if (error)
            *error = QStringLiteral("screenshot-raw 失败：%1").arg(m_api->errorString(rc));
        if (result.format != MPV_FORMAT_NONE)
            m_api->freeNodeContents(&result);
        return false;
    }

    int w = 0, h = 0, stride = 0;
    QByteArray format;
    QByteArray data;
    auto *list = result.u.list;
    for (int i = 0; i < list->num; ++i) {
        const QString key = QString::fromUtf8(list->keys[i]);
        const mpv_node &value = list->values[i];
        if (key == QLatin1String("w") && value.format == MPV_FORMAT_INT64)
            w = static_cast<int>(value.u.int64);
        else if (key == QLatin1String("h") && value.format == MPV_FORMAT_INT64)
            h = static_cast<int>(value.u.int64);
        else if (key == QLatin1String("stride") && value.format == MPV_FORMAT_INT64)
            stride = static_cast<int>(value.u.int64);
        else if (key == QLatin1String("format") && value.format == MPV_FORMAT_STRING)
            format = QByteArray(value.u.string);
        else if (key == QLatin1String("data") && value.format == MPV_FORMAT_BYTE_ARRAY) {
            data = QByteArray(reinterpret_cast<const char *>(value.u.ba->data),
                              static_cast<int>(value.u.ba->size));
        }
    }
    m_api->freeNodeContents(&result);

    if (w <= 0 || h <= 0 || stride <= 0 || data.isEmpty()) {
        if (error) *error = QStringLiteral("screenshot-raw 返回空帧");
        return false;
    }
    if (width) *width = w;
    if (height) *height = h;

    const int stepX = qMax(1, w / 64);
    const int stepY = qMax(1, h / 64);
    double sum = 0.0;
    int count = 0;
    const bool bgr = format.startsWith("bgr") || format == "bgra";
    for (int y = 0; y < h; y += stepY) {
        const uchar *row = reinterpret_cast<const uchar *>(data.constData()) + y * stride;
        for (int x = 0; x < w; x += stepX) {
            const uchar *px = row + x * 4;
            const double c0 = px[0], c1 = px[1], c2 = px[2];
            const double luma = bgr ? (0.114 * c0 + 0.587 * c1 + 0.299 * c2)
                                    : (0.299 * c0 + 0.587 * c1 + 0.114 * c2);
            sum += luma;
            ++count;
        }
    }
    if (mean) *mean = count ? sum / count : 0.0;
    return true;
}

} // namespace palune
