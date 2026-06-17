#include "ImagingView.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <QScrollBar>
#include <algorithm>

static int niceTickCount(double mmLen)
{
    if (mmLen <= 20) return 5;
    if (mmLen <= 60) return 7;
    if (mmLen <= 150) return 8;
    return 10;
}

ImagingView::ImagingView(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    m_item = new QGraphicsPixmapItem();
    m_scene->addItem(m_item);

    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);

    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(Qt::white);

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void ImagingView::setImage(const QPixmap& px, double Lx_mm, double Ly_mm)
{
    if (px.isNull()) { clearImage(); return; }

    // 物理尺寸（用于坐标轴/悬停）
    m_Lx_mm = (Lx_mm <= 1e-9) ? 1.0 : Lx_mm;
    m_Ly_mm = (Ly_mm <= 1e-9) ? 1.0 : Ly_mm;

    // 目标显示长宽比 = 物理长宽比
    const double R = m_Lx_mm / m_Ly_mm; // width/height

    // 以 viewport 为基准算一个“看得舒服”的显示尺寸（留点边给坐标轴）
    QSize vp = viewport()->size();
    int availW = std::max(1, vp.width() - 90);
    int availH = std::max(1, vp.height() - 90);

    int drawW = availW;
    int drawH = int(drawW / R + 0.5);
    if (drawH > availH) {
        drawH = availH;
        drawW = int(drawH * R + 0.5);
    }
    drawW = std::max(1, drawW);
    drawH = std::max(1, drawH);

    // 把输入图像拉到“物理比例”的像素尺寸（IgnoreAspectRatio 是关键）
    QPixmap disp = px;
    if (px.width() != drawW || px.height() != drawH) {
        disp = px.scaled(drawW, drawH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    m_pix = disp;
    m_img = disp.toImage();   // 悬停用（如果你用 setGrayForProbe，就以 probe 为准）

    m_item->setPixmap(m_pix);
    m_item->setPos(0, 0);
    m_scene->setSceneRect(QRectF(0, 0, m_pix.width(), m_pix.height()));

    updateViewFit();
    viewport()->update();
}


void ImagingView::clearImage()
{
    m_pix = QPixmap();
    m_img = QImage();
    m_item->setPixmap(QPixmap());
    m_scene->setSceneRect(QRectF(0, 0, 1, 1));
    viewport()->update();
}


void ImagingView::updateViewFit()
{
    if (m_pix.isNull()) return;

    // 留边给坐标轴/刻度
    QRectF r = m_scene->sceneRect().adjusted(-55, -35, 25, 45);
    fitInView(r, Qt::KeepAspectRatio);
}

void ImagingView::wheelEvent(QWheelEvent* e)
{
    if (m_pix.isNull()) { QGraphicsView::wheelEvent(e); return; }

    const double factor = (e->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
}

void ImagingView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastMousePos = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}


void ImagingView::mouseMoveEvent(QMouseEvent* e)
{
    // 拖拽平移
    if (m_panning) {
        QPoint delta = e->pos() - m_lastMousePos;
        m_lastMousePos = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        e->accept();
        return;
    }

    // ===== 悬停读数 =====
    if (!m_grayProbe.isNull() && m_item) {
        QPointF sp = mapToScene(e->pos());       // scene 坐标
        QRectF br = m_item->boundingRect();      // (0,0,w,h)

        if (br.contains(sp)) {
            const double u = (sp.x() - br.left()) / br.width();   // 0~1
            const double v = (sp.y() - br.top()) / br.height();  // 0~1

            const int w = m_grayProbe.width();
            const int h = m_grayProbe.height();

            const int ix = std::clamp(int(u * (w - 1)), 0, w - 1);
            const int iy = std::clamp(int(v * (h - 1)), 0, h - 1);

            // 物理坐标：y 轴向上
            const double x_mm = u * m_Lx_mm;
            const double y_mm = (1.0 - v) * m_Ly_mm;

            const uchar* line = m_grayProbe.constScanLine(iy);
            const int gray = line[ix];
            const double valueNorm = gray / 255.0;

            emit hoverInfo(x_mm, y_mm, gray, valueNorm);
        }
    }

    QGraphicsView::mouseMoveEvent(e);
}


void ImagingView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}
// ====== 生成 “好看刻度”：1/2/5 × 10^n ======
double ImagingView::niceStep(double rangeMm, int targetTicks)
{
    if (rangeMm <= 0 || targetTicks <= 0) return 1.0;

    const double raw = rangeMm / targetTicks;
    const double exp10 = std::pow(10.0, std::floor(std::log10(raw)));
    const double frac = raw / exp10;

    double niceFrac = 1.0;
    if (frac <= 1.0) niceFrac = 1.0;
    else if (frac <= 2.0) niceFrac = 2.0;
    else if (frac <= 5.0) niceFrac = 5.0;
    else niceFrac = 10.0;

    return niceFrac * exp10;
}

QVector<double> ImagingView::makeTicks(double maxMm, int targetTicks)
{
    QVector<double> ticks;
    if (maxMm <= 0) { ticks << 0.0; return ticks; }

    // 目标 5~8 个刻度比较舒服
    targetTicks = std::clamp(targetTicks, 4, 10);

    const double step = niceStep(maxMm, targetTicks);
    const double maxTick = std::ceil(maxMm / step) * step;

    for (double t = 0.0; t <= maxTick + 1e-9; t += step) {
        ticks << t;
    }
    return ticks;
}

void ImagingView::drawForeground(QPainter* p, const QRectF&)
{
    if (m_pix.isNull()) return;
    if (!m_showGrid) return;   // ★关键：网格开关

    // 图像的 sceneRect（像素坐标）
    const QRectF img = m_scene->sceneRect();
    const double x0 = img.left();
    const double y0 = img.top();
    const double x1 = img.right();
    const double y1 = img.bottom();

    // 轴线
    QPen pen(Qt::black);
    pen.setWidthF(0.0);
    p->setPen(pen);

    // y轴(左)、x轴(下)
    p->drawLine(QPointF(x0, y0), QPointF(x0, y1));
    p->drawLine(QPointF(x0, y1), QPointF(x1, y1));

    // 字体
    QFont f = p->font();
    f.setPointSize(9);
    p->setFont(f);

    // 生成“好看刻度”
    // 这里 targetTicks 用长度映射：长一点刻度多一点，但不超过 8
    const int tx = (m_Lx_mm <= 60) ? 5 : (m_Lx_mm <= 150 ? 7 : 8);
    const int ty = (m_Ly_mm <= 60) ? 5 : (m_Ly_mm <= 150 ? 7 : 8);

    const QVector<double> xt = makeTicks(m_Lx_mm, tx);
    const QVector<double> yt = makeTicks(m_Ly_mm, ty);

    // x轴刻度：避免文字挤一起，按像素间距跳标
    double lastLabelPx = -1e9;
    const double minLabelGapPx = 35.0;

    for (double mm : xt) {
        const double u = (m_Lx_mm <= 1e-9) ? 0.0 : (mm / m_Lx_mm);
        const double px = x0 + u * img.width();

        p->drawLine(QPointF(px, y1), QPointF(px, y1 + 4));

        // label 防拥挤
        if (px - lastLabelPx >= minLabelGapPx || mm == xt.last() || mm == 0.0) {
            const QString txt = (m_Lx_mm < 20.0) ? QString::number(mm, 'f', 1)
                : QString::number(mm, 'f', 0);
            p->drawText(QPointF(px - 10, y1 + 16), txt);
            lastLabelPx = px;
        }
    }
    p->drawText(QPointF((x0 + x1) / 2 - 18, y1 + 32), "x (mm)");

    // y轴刻度：同理可以加防拥挤（一般 y 不会太挤）
    for (double mm : yt) {
        const double v = (m_Ly_mm <= 1e-9) ? 0.0 : (mm / m_Ly_mm);
        const double py = y1 - v * img.height();

        p->drawLine(QPointF(x0, py), QPointF(x0 - 4, py));

        const QString txt = (m_Ly_mm < 20.0) ? QString::number(mm, 'f', 1)
            : QString::number(mm, 'f', 0);
        p->drawText(QPointF(x0 - 48, py + 4), txt);
    }
    p->drawText(QPointF(x0 - 58, y0 - 8), "y (mm)");
}

void ImagingView::setGrayForProbe(const QImage& gray)
{
    if (gray.isNull()) { m_grayProbe = QImage(); return; }

    if (gray.format() == QImage::Format_Grayscale8) {
        m_grayProbe = gray;
    }
    else {
        m_grayProbe = gray.convertToFormat(QImage::Format_Grayscale8);
    }
}

void ImagingView::setSmooth(bool on)
{
    if (m_smooth == on) return;
    m_smooth = on;
    setRenderHint(QPainter::SmoothPixmapTransform, m_smooth);
    viewport()->update();
}

void ImagingView::setInvert(bool on)
{
    if (m_invert == on) return;
    m_invert = on;

    if (!m_pix.isNull()) {
        QImage img = m_pix.toImage();
        img.invertPixels(QImage::InvertRgb);
        m_pix = QPixmap::fromImage(img);
        m_item->setPixmap(m_pix);
    }
    viewport()->update();
}

void ImagingView::setShowGrid(bool on)
{
    if (m_showGrid == on) return;
    m_showGrid = on;
    viewport()->update();
}
