#pragma once
#include <QtGlobal>
#include <QString>

/**
 * @brief 成像参数（尽量对齐你 MATLAB cfg.step5 / cfg.step6）
 * 说明：通道数建议由数据动态决定（ScanBuffer.channelCount），这里提供默认值/期望值。
 */
struct ImagingSettings final
{
    // 通道方向插值后的高度
    int interpChannels = 2048;

    // X 轴样本策略
    bool lockXSamples = true;
    int  interpSamples = 0;     // lockXSamples=false 且 >0 时生效
    double scaleX = 1.0;   // lockXSamples=false 且 interpSamples==0 时使用

    // Step5
    QString interpMethod = "bicubic";  // "linear"/"nearest"/"spline"/"bicubic"
    bool channelSharp = true;
    double sharpAlphaAmp = 10.0;
    double sharpAlphaPha = 10.0;
    double edgeGateFrac = 0.10;
    double edgePower = 1.0;

    // 物理尺寸映射（用于坐标轴）
    double dx_mm = 1.5;
    double chWidth_mm = 6.0;

    // Step6
    bool enableWavelet = true;
    int waveletLevels = 4;
    QString waveletName = "sym4";
};
