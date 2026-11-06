#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

namespace palune {

// 规范模型（M1 只定结构，不接任何服务器）。字段命名与 ARCHITECTURE §8 对齐。
struct MediaItem {
    QString id;
    QString libraryId;
    QString name;
    QString seriesName;
    QString type; // movie | episode
    int season = 0;
    int episode = 0;
    double durationSeconds = 0.0;
    QString container;
    QString videoCodec;
    QString audioCodec;
    QUrl streamUrl;
    QUrl posterUrl;
    QUrl backdropUrl;
    bool directPlayable = false;
};

struct UserState {
    QString itemId;
    double positionSeconds = 0.0;
    bool played = false;
    QDateTime lastPlayedAtUtc;
};

enum class PlaybackMode {
    DirectPlay,
    Remux,
    Transcode,
    FileFallback,
};

struct MediaCapability {
    bool auth = false;
    bool catalog = false;
    bool playback = false;
    bool progress = false;
    bool images = false;
    bool supportsHttps = false;
    QString serverVersion;
    QStringList audioCodecs;
    QStringList subtitleFormats;
};

} // namespace palune
