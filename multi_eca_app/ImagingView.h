#pragma once
#include <QGraphicsView>
#include <QGraphicsPixmapItem>

class ImagingView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImagingView(QWidget* parent = nullptr);

    // pixmap 显示 + 物理尺寸(mm)（注意：物理尺寸不随插值改变）
    void setImage(const QPixmap& px, double Lx_mm, double Ly_mm);

    void clearImage();

    // ★新增：单独给“读数用”的灰度图
    void setGrayForProbe(const QImage& gray);

    void setShowGrid(bool on);
    void setInvert(bool on);
    void setSmooth(bool on);

signals:
    void hoverInfo(double x_mm, double y_mm, int gray, double valueNorm);

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void updateViewFit();

    bool m_showGrid = true;   // 默认开
    bool m_invert = false;
    bool m_smooth = true;

private:
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;

    QPixmap m_pix;
    QImage  m_img;          // ★新增：用于取像素值
    double m_Lx_mm = 1.0;   // x: 扫描方向(mm)
    double m_Ly_mm = 1.0;   // y: 通道方向(mm)

    bool m_panning = false;
    QPoint m_lastMousePos;

    static double niceStep(double rangeMm, int targetTicks);
    static QVector<double> makeTicks(double maxMm, int targetTicks);

    QImage m_grayProbe;
};
