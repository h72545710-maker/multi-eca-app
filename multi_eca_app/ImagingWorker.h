#pragma once

#include <QObject>
#include <atomic>
#include <QMutex>

#include "ScanBuffer.h"
#include "ImagingSettings.h"
#include "ImagingResult.h"

/**
 * @brief ImagingWorker：执行 Step5（幅值/相位插值），可选 Step6（简化小波融合）
 *
 * 输入：DataProcessWorker::bufferReady(ScanBuffer)
 * 输出：resultReady(ImagingResult)
 *
 * 建议独立线程运行（插值比较耗时）。
 */
class ImagingWorker : public QObject
{
    Q_OBJECT
public:
    explicit ImagingWorker(QObject* parent = nullptr);

public slots:
    void setSettings(const ImagingSettings& s);
    void onBuffer(const ScanBuffer& buf);
    void requestStop(); // 可选：设置一个标志位，后续你想支持中断时用

signals:
    void resultReady(const ImagingResult& result);
    void imagingError(const QString& msg);

private:
    // ========== Step5：插值核心 ==========
    static double lerp(double a, double b, double t);

    // Catmull-Rom cubic（用于“bicubic/cubic”模式）
    static double cubicCR(double p0, double p1, double p2, double p3, double t);

    // 1D 采样（边界 clamp）
    static double sampleRow(const QVector<double>& row, int w, double x, bool cubic);

    // 通道方向插值（常规：linear/cubic）
    static Image2D interpAlongChannel(
        const QVector<double>& src, int srcH, int srcW,
        int outH, bool cubic,
        double dx_mm, double chWidth_mm, int channelCount);

    // 通道方向“保边双线性”（对齐你 MATLAB channel_sharp 的思路）
    static Image2D interpAlongChannelEdgeAware(
        const QVector<double>& src, int srcH, int srcW,
        int outH,
        double sharpAlpha, double edgeGateFrac, double edgePower,
        double dx_mm, double chWidth_mm, int channelCount);

    // ========== Step6：简化融合（Haar 多层） ==========
    // 注意：这是简化版（Haar），不是 sym4。用于先跑通整体链路。
    static Image2D haarFusion(const Image2D& A, const Image2D& B, int levels);

private:
    ImagingSettings m_settings;
    QMutex m_mtx;
    std::atomic<bool> m_stop{ false };

    quint64 m_jobSeq = 0;
};
