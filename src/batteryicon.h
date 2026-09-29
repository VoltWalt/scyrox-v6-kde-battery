#pragma once

#include <QIcon>
#include <QPixmap>
#include <QColor>
#include <QCache>

class BatteryIcon
{
public:
    static QIcon render(int percentage, bool charging, int size = 22);

private:
    static QColor colorForLevel(int percentage);
    static QPixmap *getCached(int size);
    static QCache<int, QPixmap> cache;
};
