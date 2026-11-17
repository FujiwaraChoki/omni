#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QVariant>

class Backend;

// Searches GIPHY for GIFs to put on a keyboard's screen. GIPHY needs an API
// key per app; omni asks for the user's own (free, 100 searches an hour) and
// keeps it in ~/.config/omni/giphy.key, readable only by them. Searches go to
// GIPHY only when the user presses Enter, and nothing else leaves the computer.
class GifSearch : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY keyChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY resultsChanged)

public:
    explicit GifSearch(Backend *backend, const QString &keyFile = {}, QObject *parent = nullptr);

    bool hasKey() const { return !m_key.isEmpty(); }
    QVariantList results() const { return m_results; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    bool canLoadMore() const { return m_total > m_results.size(); }

    Q_INVOKABLE bool setKey(const QString &key);
    Q_INVOKABLE void forgetKey();
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void loadMore();
    // Downloads a result and puts it on the current keyboard's screen.
    Q_INVOKABLE void use(int index);

    // GIPHY's search answer as result rows {id, title, preview, file}.
    static QVariantList parse(const QByteArray &json, int *total);

signals:
    void keyChanged();
    void resultsChanged();
    void busyChanged();
    void errorChanged();

private:
    void request(const QString &query, int offset);
    void setBusy(bool busy);
    void setError(const QString &error);

    Backend *m_backend;
    QString m_keyFile;
    QString m_key;
    QNetworkAccessManager m_network;
    QVariantList m_results;
    QString m_query;
    int m_total = 0;
    bool m_busy = false;
    QString m_error;
};
