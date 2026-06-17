#pragma once
#include <QtGlobal>
#include "Image2D.h"

/**
 * @brief 成像输出（给 UI 显示/给 SaveWorker 保存）
 *
 * - ampInterp / phaInterp：你明确要用的 Step5 产物
 * - fusedNorm：Step6 产物（可选），通常是 0~1 的归一化图
 */
struct ImagingResult final
{
    quint64 jobSeq = 0;
    qint64  startTimeNs = 0;
    qint64  endTimeNs = 0;

    Image2D ampInterp;
    Image2D phaInterp;

    bool hasFused = false;
    Image2D fusedNorm; // Fs
};
