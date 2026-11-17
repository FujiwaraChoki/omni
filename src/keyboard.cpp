#include "keyboard.h"

#include "apexdriver.h"
#include "wootingdriver.h"

#include <algorithm>
#include <cmath>

double Range::snap(double v) const {
    if (!stops.isEmpty())
        return *std::min_element(stops.begin(), stops.end(), [v](double a, double b) { return std::abs(a - v) < std::abs(b - v); });
    const double snapped = min + std::round((v - min) / step) * step;
    // Round to the step's precision so 0.30000000000000004 reads as 0.3.
    return std::round(qBound(min, snapped, max) * 1000) / 1000;
}

const QList<Driver> &drivers() {
    static const QList<Driver> list{
        {QStringLiteral("SteelSeries Apex Pro TKL Gen 3"), &ApexDriver::matches,
         [](const QList<HidNode> &nodes) -> Keyboard * { auto *k = new ApexDriver(nodes); k->setNodes(nodes); return k; }},
        {QStringLiteral("Wooting 60HE v2"), &WootingDriver::matches,
         [](const QList<HidNode> &nodes) -> Keyboard * { auto *k = new WootingDriver(nodes); k->setNodes(nodes); return k; }},
    };
    return list;
}
