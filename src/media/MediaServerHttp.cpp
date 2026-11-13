#include "media/MediaServerHttp.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace palune {
namespace http {

Response request(const QByteArray &verb,
                 const QUrl &url,
                 const Headers &headers,
                 const QByteArray &body,
                 int timeoutMs)
{
    Response out;
    if (!url.isValid() || url.scheme().isEmpty()) {
        out.error = QStringLiteral("无效地址");
        return out;
    }

    // 每次请求独立 QNAM：适配器可能在不同工作线程被调用，共享实例会触发线程警告。
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);

    QNetworkReply *reply = nullptr;
    if (verb == "POST")
        reply = nam.post(req, body);
    else if (verb == "PUT")
        reply = nam.put(req, body);
    else if (verb == "DELETE")
        reply = nam.deleteResource(req);
    else
        reply = nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    // 有响应时 Qt 也会给 QNetworkReply::error()（如 401 的 AuthenticationRequiredError），
    // 但 HTTP 状态码仍然可用；据此区分「认证失败」和「根本连不上」。
    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (timedOut) {
        out.error = QStringLiteral("请求超时（%1 ms）").arg(timeoutMs);
    } else if (reply->error() != QNetworkReply::NoError) {
        out.error = reply->errorString();
    }
    out.body = reply->readAll();
    reply->deleteLater();
    return out;
}

QUrl normalizeBase(const QString &raw, QString *error)
{
    QString text = raw.trimmed();
    if (text.isEmpty()) {
        if (error)
            *error = QStringLiteral("服务器地址为空");
        return {};
    }
    if (!text.startsWith(QLatin1String("http://")) && !text.startsWith(QLatin1String("https://")))
        text.prepend(QLatin1String("http://"));
    while (text.endsWith(QLatin1Char('/')))
        text.chop(1);

    QUrl url(text);
    if (!url.isValid() || url.host().isEmpty()) {
        if (error)
            *error = QStringLiteral("无法解析服务器地址：%1").arg(raw);
        return {};
    }
    return url;
}

} // namespace http
} // namespace palune
