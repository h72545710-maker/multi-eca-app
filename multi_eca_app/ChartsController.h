#pragma once
#include <QObject>
#include "ChartPanel.h"

QT_FORWARD_DECLARE_CLASS(QLabel)

class ChartsController : public QObject
{
    Q_OBJECT
public:
    explicit ChartsController(QObject* parent = nullptr);

    // 绑定 UI（你 ui 里这几个名字我已经对上了）
    void bindUi(QWidget* wAmp1, QComboBox* cbAmp1,
        QWidget* wPha1, QComboBox* cbPha1,
        QWidget* wAmp2, QComboBox* cbAmp2,
        QWidget* wPha2, QComboBox* cbPha2
       );

    void setChannelCount(int n);
    void setXPoints(int points);

    // 给某个通道塞一帧数据（缓存起来，等 QTimer 刷新时用）
    void pushSample(int ch, double ampV, double phaV);

    // 供 MainWindow 的 QTimer 调用：把“当前选择通道”的缓存画到图上
    void refresh();

private:
    int m_chCount = 7;
    int m_xPoints = 50;

    QLabel* m_labelStatus = nullptr;

    ChartPanel m_amp1;
    ChartPanel m_pha1;
    ChartPanel m_amp2;
    ChartPanel m_pha2;

    // 每通道缓存一条曲线（长度限制 m_xPoints）
    QVector<QVector<double>> m_ampBuf;
    QVector<QVector<double>> m_phaBuf;

    void ensureBuffers();
    static void pushRing(QVector<double>& v, double x, int maxN);
};
