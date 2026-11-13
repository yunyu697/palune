#include "mpv/MpvClient.h"

#include "diag/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#if defined(Q_OS_UNIX)
#include <clocale>
#endif

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
        m_loadError = QCoreApplication::translate("Palune", "libmpv 缺少导出符号：%1").arg(name);
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
#if defined(Q_OS_UNIX)
        // 部分 libmpv 构建要求 LC_NUMERIC 为 C，否则 mpv_create 直接返回空。
        std::setlocale(LC_NUMERIC, "C");
#endif
        Log::info(QStringLiteral("mpv create 开始"));
        m_mpv = m_api->create();
        Log::info(QStringLiteral("mpv create 返回：%1").arg(m_mpv ? QStringLiteral("ok") : QStringLiteral("null")));
        if (!m_mpv) {
            m_loadError = QCoreApplication::translate("Palune", "mpv_create 失败");
            m_available = false;
            return false;
        }
        applyOptions(wid);
        Log::info(QStringLiteral("mpv 选项应用完成，准备 initialize"));
        Log::info(QStringLiteral("mpv 视频窗句柄：wid=%1").arg(wid));
        const int rc = m_api->initialize(m_mpv);
        Log::info(QStringLiteral("mpv initialize 返回：%1").arg(rc));
        if (rc < 0) {
            m_loadError = QCoreApplication::translate("Palune", "mpv_initialize 失败：%1").arg(m_api->errorString(rc));
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
        const int rc = m_api->setOptionString(m_mpv, k, v);
        if (rc >= 0) {
            if (qEnvironmentVariableIsSet("PALUNE_MPV_TRACE"))
                Log::info(QStringLiteral("mpv 选项 %1=%2").arg(k, v));
            else
                Log::debug(QStringLiteral("mpv 选项 %1=%2").arg(k, v));
        } else {
            Log::warn(QStringLiteral("mpv 选项设置失败：%1=%2（%3）")
                          .arg(k, v, m_api->errorString(rc)));
        }
    };

    const QByteArray voEnv = qEnvironmentVariable("PALUNE_MPV_VO").toUtf8();
    opt("vo", voEnv.isEmpty() ? "gpu-next" : voEnv.constData());
    const QByteArray aoEnv = qEnvironmentVariable("PALUNE_MPV_AO").toUtf8();
    if (!aoEnv.isEmpty())
        opt("ao", aoEnv.constData());
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
        m_errorMessage = QCoreApplication::translate("Palune", "测试图样打开失败：%1").arg(m_api->errorString(rc));
        emit errorMessageChanged();
        emit errorOccurred(m_errorMessage);
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
        return QCoreApplication::translate("Palune", "未初始化");
    char *version = m_api->getPropertyString(m_mpv, "mpv-version");
    if (!version)
        return QCoreApplication::translate("Palune", "未知");
    const QString text = QString::fromUtf8(version);
    m_api->freeString(version);
    return text;
}

QString MpvClient::backendDescription() const
{
#if defined(Q_OS_WIN)
    return QCoreApplication::translate("Palune", "wid + d3d11 + gpu-next（Windows 主路径）");
#elif defined(Q_OS_MACOS)
    return QCoreApplication::translate("Palune", "render API (GL) — 接口已预留，M0 未在本机验证");
#else
    return QCoreApplication::translate("Palune", "render API (GL) — 接口已预留，M0 未在本机验证");
#endif
}

bool MpvClient::openFile(const QString &path)
{
    if (!m_initialized) {
        m_loadError = QCoreApplication::translate("Palune", "mpv 尚未初始化");
        return false;
    }
    // 网络地址（http/https 等）必须原样交给 mpv；QFileInfo 会把它改写成
    // 工作目录下的本地路径（http:\127.0.0.1\...），导致 loadfile 打不开。
    const bool remote = path.contains(QStringLiteral("://"));
    const QString absolute = remote ? path : QFileInfo(path).absoluteFilePath();
    const QByteArray fileUtf8 = absolute.toUtf8();
    const char *args[] = {"loadfile", fileUtf8.constData(), "replace", nullptr};
    const int rc = m_api->command(m_mpv, args);
    if (rc < 0) {
        m_errorMessage = QCoreApplication::translate("Palune", "打开文件失败：%1").arg(m_api->errorString(rc));
        emit errorMessageChanged();
        emit errorOccurred(m_errorMessage);
        return false;
    }
    m_errorMessage.clear();
    emit errorMessageChanged();
    clearVideoInfo();
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

void MpvClient::refreshFrame()
{
    if (!m_initialized || m_filePath.isEmpty())
        return;
    // 暂停态不产生新帧：窗口被其它窗口遮挡后切回焦点，DWM 会保留遮挡期间的合成结果，
    // 表现为上一个窗口的画面残留在播放器上。同位置精确 seek 会让 mpv 重新渲染当前帧，
    // 时间戳不变（不会跳帧/前进），用于激活或最小化恢复时强制重绘。
    const QByteArray value = QByteArray::number(m_position, 'f', 3);
    const char *args[] = {"seek", value.constData(), "absolute+exact", nullptr};
    if (m_api->command(m_mpv, args) < 0)
        Log::warn(QStringLiteral("刷新帧失败（暂停态 seek %1s）").arg(m_position));
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
    clearVideoInfo();
}

void MpvClient::setSpeed(double speed)
{
    if (!m_initialized)
        return;
    const double v = qBound(0.25, speed, 4.0);
    double writable = v;
    if (m_api->setProperty(m_mpv, "speed", MPV_FORMAT_DOUBLE, &writable) >= 0) {
        m_speed = v;
        emit speedChanged();
        Log::info(QStringLiteral("播放速度设为 %1x").arg(v));
    }
}

void MpvClient::frameStep(bool forward)
{
    if (!m_initialized)
        return;
    const char *args[] = {forward ? "frame-step" : "frame-back-step", nullptr};
    if (m_api->command(m_mpv, args) < 0)
        Log::warn(QStringLiteral("逐帧步进失败：%1").arg(forward ? QStringLiteral("前进") : QStringLiteral("后退")));
}

void MpvClient::addSubtitleDelay(double delta)
{
    if (!m_initialized)
        return;
    const double v = qBound(-10.0, m_subtitleDelay + delta, 10.0);
    double writable = v;
    if (m_api->setProperty(m_mpv, "sub-delay", MPV_FORMAT_DOUBLE, &writable) >= 0) {
        m_subtitleDelay = v;
        Log::info(QStringLiteral("字幕延迟 %1s").arg(v, 0, 'f', 2));
    }
}

void MpvClient::setAudioTrack(int id)
{
    if (!m_initialized)
        return;
    const QByteArray value = QByteArray::number(id);
    const char *args[] = {"set", "aid", value.constData(), nullptr};
    if (m_api->command(m_mpv, args) < 0)
        Log::warn(QStringLiteral("切换音轨失败：%1").arg(id));
    else
        Log::info(QStringLiteral("音轨切换为 %1").arg(id));
}

void MpvClient::setSubtitleTrack(int id)
{
    if (!m_initialized)
        return;
    const QByteArray value = id < 0 ? QByteArray("no") : QByteArray::number(id);
    const char *args[] = {"set", "sid", value.constData(), nullptr};
    if (m_api->command(m_mpv, args) < 0)
        Log::warn(QStringLiteral("切换字幕轨失败：%1").arg(id));
    else
        Log::info(QStringLiteral("字幕轨切换为 %1").arg(id < 0 ? QStringLiteral("关闭") : QString::number(id)));
}

bool MpvClient::setStringProperty(const char *name, const char *value)
{
    if (!m_initialized)
        return false;
    const int rc = m_api->setPropertyString(m_mpv, name, value);
    if (rc < 0) {
        Log::warn(QStringLiteral("mpv 属性写入失败：%1=%2（%3）")
                      .arg(QString::fromUtf8(name), QString::fromUtf8(value),
                           m_api->errorString(rc)));
        return false;
    }
    return true;
}

void MpvClient::setSubtitleStyle(int fontSize, const QString &color, double outline,
                                 int positionPercent)
{
    // mpv 的 sub-font-size 以 720p 为基准（默认 55），界面上的 15 对应约 54。
    const int scaled = qBound(8, qRound(fontSize * 3.6), 200);
    setStringProperty("sub-font-size", QByteArray::number(scaled).constData());
    setStringProperty("sub-color", color.toUtf8().constData());
    const int border = qBound(0, qRound(outline * 2.0), 12);
    setStringProperty("sub-border-size", QByteArray::number(border).constData());
    // sub-pos：100 为贴底，向上移动 = 减小数值。
    const int pos = qBound(0, 100 - positionPercent, 100);
    setStringProperty("sub-pos", QByteArray::number(pos).constData());
}

void MpvClient::setSubtitleDelayAbsolute(double seconds)
{
    if (!m_initialized)
        return;
    m_subtitleDelay = qBound(-10.0, seconds, 10.0);
    double writable = m_subtitleDelay;
    if (m_api->setProperty(m_mpv, "sub-delay", MPV_FORMAT_DOUBLE, &writable) < 0)
        Log::warn(QStringLiteral("字幕延迟设置失败：%1").arg(seconds));
}

void MpvClient::setSubtitleAutoLoad(bool enabled)
{
    setStringProperty("sub-auto", enabled ? "fuzzy" : "no");
}

void MpvClient::setSubtitleEncoding(const QString &encoding)
{
    // 内部值：auto / utf-8 / gb18030
    QByteArray value = "auto";
    if (encoding.compare(QLatin1String("utf-8"), Qt::CaseInsensitive) == 0)
        value = "utf-8";
    else if (encoding.compare(QLatin1String("gb18030"), Qt::CaseInsensitive) == 0)
        value = "gb18030";
    setStringProperty("sub-codepage", value.constData());
}

void MpvClient::addSubtitleFile(const QString &path)
{
    if (!m_initialized || path.isEmpty())
        return;
    const QByteArray file = QFileInfo(path).absoluteFilePath().toUtf8();
    const char *args[] = {"sub-add", file.constData(), "select", nullptr};
    if (m_api->command(m_mpv, args) < 0)
        Log::warn(QStringLiteral("外挂字幕加载失败：%1").arg(path));
    else
        Log::info(QStringLiteral("外挂字幕已加载：%1").arg(path));
}

void MpvClient::setAudioDelay(double seconds)
{
    if (!m_initialized)
        return;
    double writable = qBound(-10.0, seconds, 10.0);
    if (m_api->setProperty(m_mpv, "audio-delay", MPV_FORMAT_DOUBLE, &writable) < 0)
        Log::warn(QStringLiteral("音频延迟设置失败：%1").arg(seconds));
}

void MpvClient::setAudioChannels(const QString &channels)
{
    const QByteArray value = channels == QLatin1String("stereo") ? QByteArray("stereo")
        : (channels == QLatin1String("5.1") ? QByteArray("5.1") : QByteArray("auto"));
    setStringProperty("audio-channels", value.constData());
}

void MpvClient::setAudioDevice(const QString &device)
{
    const QByteArray value = (device.isEmpty() || device == QLatin1String("auto"))
        ? QByteArray("auto") : device.toUtf8();
    setStringProperty("audio-device", value.constData());
}

QVariantList MpvClient::audioDevices() const
{
    QVariantList devices;
    if (!m_initialized)
        return devices;
    mpv_node node;
    memset(&node, 0, sizeof(node));
    if (m_api->getProperty(m_mpv, "audio-device-list", MPV_FORMAT_NODE, &node) < 0)
        return devices;
    if (node.format != MPV_FORMAT_NODE_ARRAY) {
        m_api->freeNodeContents(&node);
        return devices;
    }
    for (int i = 0; i < node.u.list->num; ++i) {
        const mpv_node &entry = node.u.list->values[i];
        if (entry.format != MPV_FORMAT_NODE_MAP)
            continue;
        QString name;
        QString description;
        for (int j = 0; j < entry.u.list->num; ++j) {
            const QString key = QString::fromUtf8(entry.u.list->keys[j]);
            const mpv_node &value = entry.u.list->values[j];
            if (value.format != MPV_FORMAT_STRING)
                continue;
            if (key == QLatin1String("name"))
                name = QString::fromUtf8(value.u.string);
            else if (key == QLatin1String("description"))
                description = QString::fromUtf8(value.u.string);
        }
        if (name.isEmpty())
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("label"), description.isEmpty() ? name : description);
        item.insert(QStringLiteral("value"), name);
        devices.append(item);
    }
    m_api->freeNodeContents(&node);
    return devices;
}

void MpvClient::setHwdecMode(const QString &mode)
{
    QByteArray value = "auto-safe";
#if defined(Q_OS_WIN)
    // 自动：Windows 上仍是 d3d11va；显式 off 关闭硬解。
    value = "d3d11va";
#elif defined(Q_OS_MACOS)
    value = "videotoolbox";
#endif
    if (mode == QLatin1String("off"))
        value = "no";
    else if (mode == QLatin1String("hardware")) {
#if defined(Q_OS_WIN)
        value = "d3d11va";
#elif defined(Q_OS_MACOS)
        value = "videotoolbox";
#else
        value = "auto-safe";
#endif
    }
    setStringProperty("hwdec", value.constData());
}

void MpvClient::setCacheSeconds(int seconds)
{
    if (!m_initialized)
        return;
    QString cacheValue = seconds > 0 ? QStringLiteral("yes") : QStringLiteral("auto");
    setStringProperty("cache", cacheValue.toUtf8().constData());
    int64_t secs = seconds > 0 ? seconds : 10;
    if (m_api->setProperty(m_mpv, "cache-secs", MPV_FORMAT_INT64, &secs) < 0)
        Log::warn(QStringLiteral("缓存时长设置失败：%1s").arg(secs));
}

void MpvClient::setToneMapping(bool enabled, int targetPeakNits)
{
    setStringProperty("tone-mapping", enabled ? "bt.2390" : "auto");
    if (!m_initialized)
        return;
    int64_t peak = qBound(200, targetPeakNits, 2000);
    if (m_api->setProperty(m_mpv, "target-peak", MPV_FORMAT_INT64, &peak) < 0)
        Log::warn(QStringLiteral("目标峰值亮度设置失败：%1").arg(peak));
}

void MpvClient::setDeinterlace(const QString &mode)
{
    const QByteArray value = mode == QLatin1String("on") ? QByteArray("yes")
        : (mode == QLatin1String("off") ? QByteArray("no") : QByteArray("auto"));
    setStringProperty("deinterlace", value.constData());
}

QString MpvClient::propertyString(const QString &name) const
{
    if (!m_initialized || !m_api)
        return {};
    char *raw = m_api->getPropertyString(m_mpv, name.toUtf8().constData());
    if (!raw)
        return {};
    const QString value = QString::fromUtf8(raw);
    m_api->freeString(raw);
    return value;
}

void MpvClient::setLoopFile(bool enabled)
{
    if (!m_initialized)
        return;
    const char *args[] = {"set", "loop-file", enabled ? "inf" : "no", nullptr};
    if (m_api->command(m_mpv, args) < 0)
        Log::warn(QStringLiteral("设置循环播放失败"));
}

void MpvClient::poll()
{
    if (!m_initialized)
        return;
    handleEvents();
    updateProperties();
    // 技术信息低频刷新（约 0.5s 一次；poll 间隔 5ms），避免每帧取字符串属性。
    if (++m_propTick >= 100) {
        m_propTick = 0;
        updateVideoInfo();
    }
    refreshTrackList();
}

void MpvClient::clearVideoInfo()
{
    if (m_videoCodec.isEmpty() && m_hwdec.isEmpty() && m_hdrMode.isEmpty()
        && m_videoFps == 0.0 && m_videoBitrate == 0.0) {
        return;
    }
    m_videoCodec.clear();
    m_hwdec.clear();
    m_hdrMode.clear();
    m_videoFps = 0.0;
    m_videoBitrate = 0.0;
    emit videoInfoChanged();
}

void MpvClient::updateVideoInfo()
{
    auto stringProperty = [this](const char *name) {
        char *raw = m_api->getPropertyString(m_mpv, name);
        if (!raw)
            return QString();
        const QString value = QString::fromUtf8(raw);
        m_api->freeString(raw);
        return value;
    };

    const QString codec = stringProperty("video-codec");
    const QString hwdecCurrent = stringProperty("hwdec-current");
    const QString primaries = stringProperty("video-params/primaries");
    const QString gamma = stringProperty("video-params/gamma");

    QString hdr;
    if (!primaries.isEmpty() || !gamma.isEmpty()) {
        if (gamma == QLatin1String("pq"))
            hdr = QStringLiteral("HDR10");
        else if (gamma == QLatin1String("hlg"))
            hdr = QStringLiteral("HLG");
        else if (primaries == QLatin1String("bt.2020"))
            hdr = QStringLiteral("HDR");
        else
            hdr = QStringLiteral("SDR");
    }

    double fps = 0.0;
    if (m_api->getProperty(m_mpv, "container-fps", MPV_FORMAT_DOUBLE, &fps) < 0
        && m_api->getProperty(m_mpv, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &fps) < 0) {
        fps = 0.0;
    }
    double bitrate = 0.0;
    if (m_api->getProperty(m_mpv, "video-bitrate", MPV_FORMAT_DOUBLE, &bitrate) < 0)
        bitrate = 0.0;
    if (bitrate <= 0.0) {
        // 部分容器不提供 video-bitrate：用文件大小 / 时长折算整体码率（含音轨）。
        int64_t fileSize = 0;
        double duration = 0.0;
        if (m_api->getProperty(m_mpv, "file-size", MPV_FORMAT_INT64, &fileSize) >= 0
            && m_api->getProperty(m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration) >= 0
            && duration > 0.0) {
            bitrate = static_cast<double>(fileSize) * 8.0 / duration;
        }
    }

    if (codec == m_videoCodec && hwdecCurrent == m_hwdec && hdr == m_hdrMode
        && qFuzzyCompare(fps + 1.0, m_videoFps + 1.0)
        && qFuzzyCompare(bitrate + 1.0, m_videoBitrate + 1.0)) {
        return;
    }
    m_videoCodec = codec;
    m_hwdec = hwdecCurrent;
    m_hdrMode = hdr;
    m_videoFps = fps;
    m_videoBitrate = bitrate;
    emit videoInfoChanged();
}

void MpvClient::handleEvents()
{
    while (true) {
        mpv_event *event = m_api->waitEvent(m_mpv, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_START_FILE:
            ++m_startFileCount;
            emit playbackEventsChanged();
            break;
        case MPV_EVENT_FILE_LOADED:
            m_pendingFile = false;
            emit fileLoaded();
            break;
        case MPV_EVENT_END_FILE: {
            ++m_endFileCount;
            emit playbackEventsChanged();
            auto *end = static_cast<mpv_event_end_file *>(event->data);
            m_paused = true;
            emit pausedChanged();
            if (end && end->reason == MPV_END_FILE_REASON_ERROR) {
                const QString msg = QStringLiteral("播放失败：%1")
                                        .arg(m_api->errorString(end->error));
                m_errorMessage = msg;
                emit errorMessageChanged();
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

    if (m_api->getProperty(m_mpv, "speed", MPV_FORMAT_DOUBLE, &d) >= 0
        && !qFuzzyCompare(d + 1.0, m_speed + 1.0)) {
        m_speed = d;
        emit speedChanged();
    }
    if (m_api->getProperty(m_mpv, "audio-pts", MPV_FORMAT_DOUBLE, &d) >= 0)
        m_audioPts = d;

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

void MpvClient::refreshTrackList()
{
    int64_t count = 0;
    if (m_api->getProperty(m_mpv, "track-list/count", MPV_FORMAT_INT64, &count) < 0)
        return;
    if (count == m_trackCount)
        return;
    m_trackCount = count;

    mpv_node node;
    memset(&node, 0, sizeof(node));
    if (m_api->getProperty(m_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
        return;
    if (node.format != MPV_FORMAT_NODE_ARRAY) {
        m_api->freeNodeContents(&node);
        return;
    }

    QVariantList audio;
    QVariantList subtitles;
    for (int i = 0; i < node.u.list->num; ++i) {
        const mpv_node &entry = node.u.list->values[i];
        if (entry.format != MPV_FORMAT_NODE_MAP)
            continue;
        QVariantMap track;
        QString type;
        for (int j = 0; j < entry.u.list->num; ++j) {
            const QString key = QString::fromUtf8(entry.u.list->keys[j]);
            const mpv_node &value = entry.u.list->values[j];
            if (key == QLatin1String("id") && value.format == MPV_FORMAT_INT64)
                track.insert(QStringLiteral("id"), static_cast<int>(value.u.int64));
            else if (key == QLatin1String("type") && value.format == MPV_FORMAT_STRING)
                type = QString::fromUtf8(value.u.string);
            else if (key == QLatin1String("title") && value.format == MPV_FORMAT_STRING)
                track.insert(QStringLiteral("title"), QString::fromUtf8(value.u.string));
            else if (key == QLatin1String("lang") && value.format == MPV_FORMAT_STRING)
                track.insert(QStringLiteral("lang"), QString::fromUtf8(value.u.string));
            else if (key == QLatin1String("codec") && value.format == MPV_FORMAT_STRING)
                track.insert(QStringLiteral("codec"), QString::fromUtf8(value.u.string));
            else if (key == QLatin1String("selected") && value.format == MPV_FORMAT_FLAG)
                track.insert(QStringLiteral("selected"), value.u.flag != 0);
            else if (key == QLatin1String("external") && value.format == MPV_FORMAT_FLAG)
                track.insert(QStringLiteral("external"), value.u.flag != 0);
        }
        if (track.contains(QStringLiteral("id"))) {
            track.insert(QStringLiteral("type"), type);
            if (type == QLatin1String("audio"))
                audio << track;
            else if (type == QLatin1String("sub"))
                subtitles << track;
        }
    }
    m_api->freeNodeContents(&node);

    m_audioTracks = audio;
    m_subtitleTracks = subtitles;
    emit tracksChanged();
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
