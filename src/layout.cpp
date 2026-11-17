#include "layout.h"

#include <QHash>

namespace {

struct KeyInfo { const char *code; const char *label; quint8 usage; };

// HID usage tables, section 10 (Keyboard/Keypad page 0x07).
const KeyInfo kKeys[] = {
    {"KeyA", "A", 0x04}, {"KeyB", "B", 0x05}, {"KeyC", "C", 0x06}, {"KeyD", "D", 0x07},
    {"KeyE", "E", 0x08}, {"KeyF", "F", 0x09}, {"KeyG", "G", 0x0A}, {"KeyH", "H", 0x0B},
    {"KeyI", "I", 0x0C}, {"KeyJ", "J", 0x0D}, {"KeyK", "K", 0x0E}, {"KeyL", "L", 0x0F},
    {"KeyM", "M", 0x10}, {"KeyN", "N", 0x11}, {"KeyO", "O", 0x12}, {"KeyP", "P", 0x13},
    {"KeyQ", "Q", 0x14}, {"KeyR", "R", 0x15}, {"KeyS", "S", 0x16}, {"KeyT", "T", 0x17},
    {"KeyU", "U", 0x18}, {"KeyV", "V", 0x19}, {"KeyW", "W", 0x1A}, {"KeyX", "X", 0x1B},
    {"KeyY", "Y", 0x1C}, {"KeyZ", "Z", 0x1D},
    {"Digit1", "1", 0x1E}, {"Digit2", "2", 0x1F}, {"Digit3", "3", 0x20}, {"Digit4", "4", 0x21},
    {"Digit5", "5", 0x22}, {"Digit6", "6", 0x23}, {"Digit7", "7", 0x24}, {"Digit8", "8", 0x25},
    {"Digit9", "9", 0x26}, {"Digit0", "0", 0x27},
    {"Enter", "Enter", 0x28}, {"Escape", "Esc", 0x29}, {"Backspace", "Back", 0x2A}, {"Tab", "Tab", 0x2B},
    {"Space", "", 0x2C}, {"Minus", "-", 0x2D}, {"Equal", "=", 0x2E}, {"BracketLeft", "[", 0x2F},
    {"BracketRight", "]", 0x30}, {"Backslash", "\\", 0x31}, {"IntlHash", "#", 0x32}, {"Semicolon", ";", 0x33},
    {"Quote", "'", 0x34}, {"Backquote", "`", 0x35}, {"Comma", ",", 0x36}, {"Period", ".", 0x37},
    {"Slash", "/", 0x38}, {"CapsLock", "Caps", 0x39},
    {"F1", "F1", 0x3A}, {"F2", "F2", 0x3B}, {"F3", "F3", 0x3C}, {"F4", "F4", 0x3D}, {"F5", "F5", 0x3E},
    {"F6", "F6", 0x3F}, {"F7", "F7", 0x40}, {"F8", "F8", 0x41}, {"F9", "F9", 0x42}, {"F10", "F10", 0x43},
    {"F11", "F11", 0x44}, {"F12", "F12", 0x45},
    {"PrintScreen", "PrtSc", 0x46}, {"ScrollLock", "ScrLk", 0x47}, {"Pause", "Pause", 0x48},
    {"Insert", "Ins", 0x49}, {"Home", "Home", 0x4A}, {"PageUp", "PgUp", 0x4B}, {"Delete", "Del", 0x4C},
    {"End", "End", 0x4D}, {"PageDown", "PgDn", 0x4E}, {"ArrowRight", "→", 0x4F}, {"ArrowLeft", "←", 0x50},
    {"ArrowDown", "↓", 0x51}, {"ArrowUp", "↑", 0x52},
    {"IntlBackslash", "\\", 0x64}, {"ContextMenu", "Menu", 0x65},
    {"ControlLeft", "Ctrl", 0xE0}, {"ShiftLeft", "Shift", 0xE1}, {"AltLeft", "Alt", 0xE2}, {"MetaLeft", "Super", 0xE3},
    {"ControlRight", "Ctrl", 0xE4}, {"ShiftRight", "Shift", 0xE5}, {"AltRight", "Alt", 0xE6}, {"MetaRight", "Super", 0xE7},
    // Not standard usages: the vendor Fn key (0xF0 on SteelSeries) and the
    // Apex's media key above the navigation cluster (0xFB there).
    {"Fn", "Fn", 0xF0}, {"MediaPlayPause", "Media", 0xFB},
};

const QHash<QString, const KeyInfo *> &catalog() {
    static const QHash<QString, const KeyInfo *> map = [] {
        QHash<QString, const KeyInfo *> m;
        for (const auto &k : kKeys)
            m.insert(QString::fromLatin1(k.code), &k);
        return m;
    }();
    return map;
}

class Builder {
public:
    QList<KeyGeometry> keys;
    double x = 0, y = 0;
    void row(double top) { x = 0; y = top; }
    void gap(double u) { x += u; }
    void key(const QString &code, double w = 1, double h = 1) {
        keys << KeyGeometry{code, keyLabel(code), x, y, w, h};
        x += w;
    }
    void at(const QString &code, double kx, double ky, double w = 1, double h = 1) {
        keys << KeyGeometry{code, keyLabel(code), kx, ky, w, h};
    }
};

} // namespace

quint8 hidUsage(const QString &code) {
    const auto *info = catalog().value(code);
    return info ? info->usage : 0;
}

QString keyLabel(const QString &code) {
    const auto *info = catalog().value(code);
    return info ? QString::fromUtf8(info->label) : code;
}

QList<KeyGeometry> buildLayout(Form form, Region region, const QStringList &rightModifiers) {
    Builder b;
    const bool tkl = form == Form::Tkl;
    const bool iso = region == Region::Iso;
    // A TKL's function row sits half a key above the number row.
    const double top = tkl ? 1.25 : 0;

    if (tkl) {
        b.row(0);
        b.key("Escape"); b.gap(1);
        for (const char *f : {"F1", "F2", "F3", "F4"}) b.key(f);
        b.gap(0.5);
        for (const char *f : {"F5", "F6", "F7", "F8"}) b.key(f);
        b.gap(0.5);
        for (const char *f : {"F9", "F10", "F11", "F12"}) b.key(f);
    }

    b.row(top);
    b.key(tkl ? "Backquote" : "Escape");
    for (const char *d : {"Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "Digit0", "Minus", "Equal"})
        b.key(d);
    b.key("Backspace", 2);

    b.row(top + 1);
    b.key("Tab", 1.5);
    for (const char *k : {"KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO", "KeyP", "BracketLeft", "BracketRight"})
        b.key(k);
    if (iso)
        b.key("Enter", 1.5, 2); // drawn as one tall key; the notch is left out
    else
        b.key("Backslash", 1.5);

    b.row(top + 2);
    b.key("CapsLock", 1.75);
    for (const char *k : {"KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK", "KeyL", "Semicolon", "Quote"})
        b.key(k);
    if (iso)
        b.key("IntlHash");
    else
        b.key("Enter", 2.25);

    b.row(top + 3);
    if (iso) {
        b.key("ShiftLeft", 1.25);
        b.key("IntlBackslash");
    } else {
        b.key("ShiftLeft", 2.25);
    }
    for (const char *k : {"KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash"})
        b.key(k);
    b.key("ShiftRight", 2.75);

    b.row(top + 4);
    b.key("ControlLeft", 1.25);
    b.key("MetaLeft", 1.25);
    b.key("AltLeft", 1.25);
    b.key("Space", 6.25);
    // The right modifiers share what is left of the 15u row equally.
    const double width = (15.0 - b.x) / qMax(1, int(rightModifiers.size()));
    for (const auto &code : rightModifiers)
        b.key(code, width);

    if (tkl) {
        const double nav = 15.25;
        b.at("MediaPlayPause", nav + 2, 0);
        b.at("Insert", nav, top);     b.at("Home", nav + 1, top);     b.at("PageUp", nav + 2, top);
        b.at("Delete", nav, top + 1); b.at("End", nav + 1, top + 1);  b.at("PageDown", nav + 2, top + 1);
        b.at("ArrowUp", nav + 1, top + 3);
        b.at("ArrowLeft", nav, top + 4); b.at("ArrowDown", nav + 1, top + 4); b.at("ArrowRight", nav + 2, top + 4);
    }
    return b.keys;
}
