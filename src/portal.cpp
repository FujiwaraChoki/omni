#include "portal.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QRandomGenerator>
#include <QUrl>
#include <QVariantMap>

namespace {

// The portal's filter type: a(sa(us)), a name and its (kind, pattern) pairs; kind 0 is a glob.
struct Pattern { uint kind = 0; QString pattern; };
struct Filter { QString name; QList<Pattern> patterns; };

QDBusArgument &operator<<(QDBusArgument &a, const Pattern &p) { a.beginStructure(); a << p.kind << p.pattern; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, Pattern &p) { a.beginStructure(); a >> p.kind >> p.pattern; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const Filter &f) { a.beginStructure(); a << f.name << f.patterns; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, Filter &f) { a.beginStructure(); a >> f.name >> f.patterns; a.endStructure(); return a; }

} // namespace

Q_DECLARE_METATYPE(Pattern)
Q_DECLARE_METATYPE(Filter)

namespace {

// Receives the Request's Response signal once, then deletes itself.
class Waiter : public QObject {
    Q_OBJECT
public:
    Waiter(std::function<void(const QString &)> done, QObject *parent) : QObject(parent), m_done(std::move(done)) {}
    void finish(const QString &path) {
        if (m_done)
            m_done(path);
        m_done = nullptr;
        deleteLater();
    }
public slots:
    void response(uint code, const QVariantMap &results) {
        const QStringList uris = results.value("uris").toStringList();
        finish(code == 0 && !uris.isEmpty() ? QUrl(uris.first()).toLocalFile() : QString());
    }
private:
    std::function<void(const QString &)> m_done;
};

} // namespace

void openFileWithPortal(const QString &title, const QString &filterName, const QStringList &patterns,
                        QObject *context, std::function<void(const QString &)> done) {
    qDBusRegisterMetaType<Pattern>();
    qDBusRegisterMetaType<QList<Pattern>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<QList<Filter>>();

    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *waiter = new Waiter(std::move(done), context);

    // Listen on the request path before asking, so a fast answer is not missed.
    const QString token = QStringLiteral("omni%1").arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString request = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect("org.freedesktop.portal.Desktop", request, "org.freedesktop.portal.Request", "Response",
                waiter, SLOT(response(uint, QVariantMap)));

    Filter filter{filterName, {}};
    for (const auto &p : patterns)
        filter.patterns << Pattern{0, p};
    QVariantMap options{{"handle_token", token}, {"modal", true},
                        {"filters", QVariant::fromValue(QList<Filter>{filter})}};

    QDBusInterface chooser("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                           "org.freedesktop.portal.FileChooser", bus);
    auto *call = new QDBusPendingCallWatcher(chooser.asyncCall("OpenFile", QString(), title, options), waiter);
    QObject::connect(call, &QDBusPendingCallWatcher::finished, waiter, [waiter](QDBusPendingCallWatcher *w) {
        if (QDBusPendingReply<QDBusObjectPath>(*w).isError())
            waiter->finish({});
        w->deleteLater();
    });
}

#include "portal.moc"
