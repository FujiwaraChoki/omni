#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

QString DeviceConfig::directory() {
    const QString overridden = qEnvironmentVariable("OMNI_CONFIG_DIR");
    return !overridden.isEmpty() ? overridden
                                 : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omni";
}

QString DeviceConfig::path(const QString &directory, const QString &identity) {
    QString name = identity;
    name.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
    return directory + "/" + name + ".json";
}

DeviceConfig DeviceConfig::load(const QString &file) {
    DeviceConfig c;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return c;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    c.model = o.value("model").toString();
    c.lighting = Lighting::fromJson(o.value("lighting").toObject());
    c.screen = ScreenSettings::fromJson(o.value("screen").toObject());
    // Before lighting effects, omni kept only per-key colours.
    const QJsonObject colors = o.value("colors").toObject();
    if (!o.contains("lighting") && !colors.isEmpty()) {
        c.lighting.effect = "static";
        for (auto it = colors.begin(); it != colors.end(); ++it)
            if (QColor color(it.value().toString()); color.isValid())
                c.lighting.keyColors.insert(it.key(), color);
    }
    return c;
}

bool DeviceConfig::save(const QString &file) const {
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{{"model", model}, {"lighting", lighting.toJson()}, {"screen", screen.toJson()}}).toJson());
    return f.commit();
}
