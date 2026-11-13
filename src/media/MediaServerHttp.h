#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

namespace palune {
namespace http {

// 同步 HTTP（阻塞到响应或超时）。适配器契约是同步的，调用方保证在工作线程或
// 明确的“连接中”状态下调用，避免阻塞 UI 太久。
struct Response
{
    int status = 0;
    QByteArray body;
    QString error;

    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

using Headers = QList<QPair<QByteArray, QByteArray>>;

Response request(const QByteArray &verb,
                 const QUrl &url,
                 const Headers &headers = {},
                 const QByteArray &body = {},
                 int timeoutMs = 10000);

// 规范化服务器地址：去尾部斜杠、补默认端口；只接受 http/https。
QUrl normalizeBase(const QString &raw, QString *error);

} // namespace http
} // namespace palune
