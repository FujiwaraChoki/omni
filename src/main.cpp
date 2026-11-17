// omni — tune analog keyboards (actuation, Rapid Trigger, per-key colour) on Omarchy.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QLockFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QUrl>
#include <csignal>
#include <cstdio>
#include <unistd.h>

#include "backend.h"
#include "service.h"
#include "theme.h"
#include "translator.h"

namespace {
int signalPipe[2] = {-1, -1};
void onSignal(int) { const char c = 1; [[maybe_unused]] auto n = ::write(signalPipe[1], &c, 1); }

// `omni lighting`: the background service that draws lighting and screen
// pictures. systemd stops it with SIGTERM; it hands the keyboards back first.
int runLighting(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName("omni");
    QLockFile lock(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/omni-lighting.lock");
    if (!lock.tryLock(0)) {
        fprintf(stderr, "omni lighting is already running\n");
        return 0;
    }
    if (::pipe(signalPipe) == 0) {
        for (int sig : {SIGTERM, SIGINT, SIGHUP})
            std::signal(sig, onSignal);
    }
    LightingService service;
    QSocketNotifier stop(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&stop, &QSocketNotifier::activated, &app, [&] {
        service.release();
        app.quit();
    });
    return app.exec();
}
} // namespace

int main(int argc, char *argv[]) {
    if (argc > 1 && QByteArray(argv[1]) == "lighting")
        return runLighting(argc, argv);

    QGuiApplication app(argc, argv);
    app.setApplicationName("omni");
    app.setApplicationVersion("0.1.0");

    // Matches the window to omni.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("omni");
    app.setWindowIcon(QIcon::fromTheme("omni"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::flush);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
