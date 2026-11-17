#pragma once

#include <QString>

#include "effects.h"
#include "screen.h"

// Everything omni keeps for one keyboard on the computer, in
// ~/.config/omni/<keyboard>.json. The window writes it; the lighting service
// watches it and draws what it says.
struct DeviceConfig {
    QString model;
    Lighting lighting;
    ScreenSettings screen;

    static QString directory(); // ~/.config/omni, or $OMNI_CONFIG_DIR
    static QString path(const QString &directory, const QString &identity);
    static DeviceConfig load(const QString &file);
    bool save(const QString &file) const; // atomically: written beside, then renamed
};
