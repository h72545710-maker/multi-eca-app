#pragma once
#include <QtGlobal>
#include <QVector>

/**
 * @brief 二维图像矩阵（用于 Step5 插值结果与 Step6 融合结果）
 * 数据为 double 标量场（你当前阶段统一用“电压值”或归一化值）。
 */
struct Image2D final
{
    int height = 0;            // H
    int width = 0;            // W
    QVector<double> data;      // size = H * W，row-major

    // 物理坐标间距（用于 UI 坐标轴显示，不影响算法）
    double dx_mm = 0.0;
    double dy_mm = 0.0;

    inline bool isValid() const
    {
        return height > 0 && width > 0 && data.size() == height * width;
    }

    inline double& at(int y, int x) { return data[y * width + x]; }
    inline double  at(int y, int x) const { return data[y * width + x]; }
};
