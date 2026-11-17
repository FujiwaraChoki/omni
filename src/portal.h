#pragma once

#include <QObject>
#include <QStringList>
#include <functional>

// Asks the desktop for a file through xdg-desktop-portal's FileChooser, so the
// picker looks and behaves like every other app's. `done` gets a local path,
// or "" when the user cancels or no portal answers.
void openFileWithPortal(const QString &title, const QString &filterName, const QStringList &patterns,
                        QObject *context, std::function<void(const QString &)> done);
