#include "gifsearch.h"

#include "backend.h"
#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrlQuery>

namespace {
constexpr int kPage = 50;      // GIPHY's most per call
constexpr int kStrip = 18;     // results a search tries to show
constexpr int kReadAhead = 4;  // pages one search may use, of 100 calls an hour
} // namespace

GifSearch::GifSearch(Backend *backend, const QString &keyFile, QObject *parent)
    : QObject(parent), m_backend(backend),
      m_keyFile(keyFile.isEmpty() ? DeviceConfig::directory() + "/giphy.key" : keyFile) {
    QFile file(m_keyFile);
    if (file.open(QIODevice::ReadOnly))
        m_key = QString::fromUtf8(file.readAll()).trimmed();
}

void GifSearch::setBusy(bool busy) {
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void GifSearch::setError(const QString &error) {
    if (error != m_error) {
        m_error = error;
        emit errorChanged();
    }
}

bool GifSearch::setKey(const QString &key) {
    const QString k = key.trimmed();
    // GIPHY keys are 32 letters and digits; anything else is a paste gone wrong.
    if (!QRegularExpression("^[A-Za-z0-9]{20,64}$").match(k).hasMatch()) {
        setError(tr("That doesn't look like a GIPHY API key"));
        return false;
    }
    QDir().mkpath(QFileInfo(m_keyFile).absolutePath());
    QSaveFile file(m_keyFile);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("Could not save the key"));
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(k.toUtf8() + "\n");
    if (!file.commit()) {
        setError(tr("Could not save the key"));
        return false;
    }
    m_key = k;
    setError({});
    emit keyChanged();
    search({}); // trending, which also proves the key works
    return true;
}

void GifSearch::forgetKey() {
    QFile::remove(m_keyFile);
    m_key.clear();
    m_results.clear();
    emit keyChanged();
    emit resultsChanged();
}

QVariantList GifSearch::parse(const QByteArray &json, int *total) {
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (total)
        *total = root.value("pagination").toObject().value("total_count").toInt();
    QVariantList rows;
    for (const auto &v : root.value("data").toArray()) {
        const QJsonObject gif = v.toObject();
        const QJsonObject images = gif.value("images").toObject();
        // A small looping preview for the strip; for the screen, GIPHY's
        // 200-pixel-wide rendition is already more than 128 x 40 needs.
        const QString preview = images.value("fixed_height_small").toObject().value("url").toString();
        QString file = images.value("fixed_width").toObject().value("url").toString();
        if (file.isEmpty())
            file = images.value("downsized").toObject().value("url").toString();
        if (preview.isEmpty() || file.isEmpty())
            continue;
        const QJsonObject original = images.value("original").toObject();
        const QJsonObject shape = original.contains("width") ? original : images.value("fixed_width").toObject();
        const double w = shape.value("width").toString().toDouble(), hgt = shape.value("height").toString().toDouble();
        rows << QVariantMap{{"id", gif.value("id").toString()}, {"title", gif.value("title").toString()},
                            {"preview", preview}, {"file", file},
                            {"width", images.value("fixed_height_small").toObject().value("width").toString().toInt()},
                            {"aspect", hgt > 0 ? w / hgt : 1.0}};
    }
    return rows;
}

void GifSearch::search(const QString &query) {
    m_query = query.trimmed();
    m_results.clear();
    m_all.clear();
    m_total = 0;
    m_nextOffset = 0;
    emit resultsChanged();
    request(m_query, 0, kReadAhead, kStrip);
}

void GifSearch::loadMore() {
    if (!m_busy && canLoadMore())
        request(m_query, m_nextOffset, kReadAhead, int(m_results.size()) + kStrip);
}

void GifSearch::setWideOnly(bool wide) {
    if (wide == m_wideOnly)
        return;
    m_wideOnly = wide;
    m_results.clear();
    for (const auto &row : m_all)
        if (!m_wideOnly || row.toMap().value("aspect").toDouble() >= kWide)
            m_results << row;
    emit resultsChanged();
}

void GifSearch::request(const QString &query, int offset, int pagesLeft, int wanted) {
    if (m_key.isEmpty())
        return;
    QUrl url(query.isEmpty() ? "https://api.giphy.com/v1/gifs/trending" : "https://api.giphy.com/v1/gifs/search");
    QUrlQuery q;
    q.addQueryItem("api_key", m_key);
    if (!query.isEmpty())
        q.addQueryItem("q", query);
    q.addQueryItem("limit", QString::number(kPage));
    q.addQueryItem("offset", QString::number(offset));
    q.addQueryItem("rating", "pg-13");
    url.setQuery(q);
    setBusy(true);
    setError({});
    QNetworkReply *reply = m_network.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, query, offset, pagesLeft, wanted] {
        reply->deleteLater();
        setBusy(false);
        if (query != m_query)
            return; // an answer to a search the user has since replaced
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            setError(tr("GIPHY did not accept the key"));
            return;
        }
        if (status == 429) {
            setError(tr("GIPHY's free key allows 100 searches an hour; try again later"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setError(tr("Could not reach GIPHY: %1").arg(reply->errorString()));
            return;
        }
        int total = 0;
        const QVariantList rows = parse(reply->readAll(), &total);
        m_total = total;
        m_nextOffset = offset + kPage;
        m_all += rows;
        for (const auto &row : rows)
            if (!m_wideOnly || row.toMap().value("aspect").toDouble() >= kWide)
                m_results << row;
        emit resultsChanged();
        // Read ahead while the strip is short and this search has pages left.
        if (m_results.size() < wanted && pagesLeft > 1 && m_nextOffset < m_total && !rows.isEmpty())
            request(query, m_nextOffset, pagesLeft - 1, wanted);
    });
}

void GifSearch::use(int index) {
    if (index < 0 || index >= m_results.size())
        return;
    const QUrl url(m_results[index].toMap().value("file").toString());
    const QString title = m_results[index].toMap().value("title").toString();
    // Only ever fetch pictures from GIPHY's own media hosts.
    if (url.scheme() != "https" || !url.host().endsWith(".giphy.com")) {
        setError(tr("Unexpected address for that GIF"));
        return;
    }
    setBusy(true);
    setError({});
    QNetworkReply *reply = m_network.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, title] {
        reply->deleteLater();
        setBusy(false);
        if (reply->error() != QNetworkReply::NoError) {
            setError(tr("Could not download the GIF: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() > 20 * 1024 * 1024 || !data.startsWith("GIF8")) {
            setError(tr("GIPHY sent something that isn't a GIF"));
            return;
        }
        // The backend keeps its own copy; this file only bridges the download.
        QTemporaryFile file(QDir::tempPath() + "/omni-XXXXXX.gif");
        if (!file.open() || file.write(data) != data.size()) {
            setError(tr("Could not save the GIF"));
            return;
        }
        file.close();
        // GIPHY's GIFs are mostly square-ish and the screen is wide, so fill it
        // rather than show a sliver; Fit is one click away.
        m_backend->setScreenPicture(file.fileName(), title.isEmpty() ? tr("GIF from GIPHY") : title, "fill");
    });
}
