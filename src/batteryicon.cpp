#include "batteryicon.h"
#include <QPainter>

QCache<int, QPixmap> BatteryIcon::cache(10);

QPixmap *BatteryIcon::getCached(int size)
{
    QPixmap *pm = cache.object(size);
    if (!pm) {
        pm = new QPixmap(size, size);
        cache.insert(size, pm);
    }
    return pm;
}

QColor BatteryIcon::colorForLevel(int percentage)
{
    if (percentage > 50) return QColor(80, 200, 80);   // green
    if (percentage > 20) return QColor(255, 200, 40);  // yellow
    return QColor(230, 60, 60);                        // red
}

QIcon BatteryIcon::render(int percentage, bool charging, int size)
{
    QPixmap *pm = getCached(size);
    pm->fill(Qt::transparent);

    QPainter p(pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = size;
    const int h = size;
    const int bodyW = w * 0.72;
    const int bodyH = h * 0.56;
    const int x = (w - bodyW) / 2;
    const int y = (h - bodyH) / 2;
    const int tipW = w * 0.12;
    const int tipH = h * 0.28;
    const int border = qMax(1, w / 14);

    // Battery body outline
    QRect bodyRect(x, y, bodyW, bodyH);
    p.setPen(QPen(Qt::white, border));
    p.setBrush(Qt::transparent);
    p.drawRoundedRect(bodyRect, border, border);

    // Battery tip
    QRect tipRect(x + bodyW, y + (bodyH - tipH) / 2, tipW, tipH);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(tipRect, border / 2, border / 2);

    // Fill level
    int pct = qBound(0, percentage, 100);
    int fillW = qMax(border, (int)((bodyW - border * 2) * pct / 100.0));
    QRect fillRect(x + border, y + border, fillW, bodyH - border * 2);

    QColor fillColor = colorForLevel(percentage);
    p.setBrush(fillColor);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(fillRect, border / 2, border / 2);

    // Charging bolt
    if (charging) {
        p.setPen(QPen(QColor(255, 220, 50), qMax(1, w / 12)));
        p.setBrush(QColor(255, 220, 50));
        int cx = x + bodyW / 2;
        int cy = y + bodyH / 2;
        int s = bodyH / 3;
        QPolygon bolt;
        bolt << QPoint(cx + s/3, cy - s)
             << QPoint(cx - s/2, cy + s/4)
             << QPoint(cx, cy + s/4)
             << QPoint(cx - s/3, cy + s)
             << QPoint(cx + s/2, cy - s/4)
             << QPoint(cx, cy - s/4);
        p.drawPolygon(bolt);
    }

    p.end();

    QIcon icon;
    icon.addPixmap(*pm);
    return icon;
}
