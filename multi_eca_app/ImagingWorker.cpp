#include "ImagingWorker.h"

#include <QtGlobal>
#include <QVector>
#include <QString>
#include <algorithm>
#include <cmath>

ImagingWorker::ImagingWorker(QObject* parent)
    : QObject(parent)
{
}

static inline void smoothX_121(Image2D& img)
{
    if (!img.isValid() || img.width < 3) return;

    const int H = img.height;
    const int W = img.width;

    QVector<double> tmp;
    tmp.resize(img.data.size());

    for (int y = 0; y < H; ++y) {
        const int row = y * W;

        tmp[row + 0] = img.data[row + 0];
        for (int x = 1; x < W - 1; ++x) {
            tmp[row + x] =
                (img.data[row + x - 1] + 2.0 * img.data[row + x] + img.data[row + x + 1]) * 0.25;
        }
        tmp[row + (W - 1)] = img.data[row + (W - 1)];
    }

    img.data.swap(tmp);
}


void ImagingWorker::setSettings(const ImagingSettings& s)
{
    QMutexLocker lk(&m_mtx);
    m_settings = s;
}

void ImagingWorker::requestStop()
{
    m_stop.store(true);
}

static inline double clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

double ImagingWorker::lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

double ImagingWorker::cubicCR(double p0, double p1, double p2, double p3, double t)
{
    // Catmull-Rom spline
    const double t2 = t * t;
    const double t3 = t2 * t;
    return 0.5 * ((2.0 * p1) +
        (-p0 + p2) * t +
        (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
        (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

double ImagingWorker::sampleRow(const QVector<double>& row, int w, double x, bool cubic)
{
    if (w <= 1) return (w == 1) ? row[0] : 0.0;

    x = clampd(x, 0.0, double(w - 1));
    const int x1 = int(std::floor(x));
    const double t = x - x1;

    if (!cubic) {
        const int x2 = std::min(w - 1, x1 + 1);
        return lerp(row[x1], row[x2], t);
    }
    else {
        const int xm1 = std::max(0, x1 - 1);
        const int x2 = std::min(w - 1, x1 + 1);
        const int x3 = std::min(w - 1, x1 + 2);
        return cubicCR(row[xm1], row[x1], row[x2], row[x3], t);
    }
}

Image2D ImagingWorker::interpAlongChannel(
    const QVector<double>& src, int srcH, int srcW,
    int outH, bool cubic,
    double dx_mm, double chWidth_mm, int channelCount)
{
    Image2D out;
    out.height = outH;
    out.width = srcW;
    out.data.resize(out.height * out.width);

    // 物理坐标
    out.dx_mm = dx_mm;
    const double total_h_mm = chWidth_mm * std::max(0, channelCount - 1);
    out.dy_mm = (out.height > 1) ? (total_h_mm / double(out.height - 1)) : 0.0;

    if (srcH <= 0 || srcW <= 0 || outH <= 0) return out;

    // y 方向重采样：y' in [0, srcH-1]
    for (int oy = 0; oy < outH; ++oy) {
        const double y = (outH == 1) ? 0.0 : (double(oy) * (double(srcH - 1) / double(outH - 1)));
        const int y1 = int(std::floor(y));
        const double t = y - y1;

        const int y2 = std::min(srcH - 1, y1 + 1);

        const int ym1 = std::max(0, y1 - 1);
        const int y3 = std::min(srcH - 1, y1 + 2);

        for (int x = 0; x < srcW; ++x) {
            const double v =
                (!cubic)
                ? lerp(src[y1 * srcW + x], src[y2 * srcW + x], t)
                : cubicCR(src[ym1 * srcW + x], src[y1 * srcW + x], src[y2 * srcW + x], src[y3 * srcW + x], t);

            out.data[oy * out.width + x] = v;
        }
    }
    return out;
}

Image2D ImagingWorker::interpAlongChannelEdgeAware(
    const QVector<double>& src, int srcH, int srcW,
    int outH,
    double sharpAlpha, double edgeGateFrac, double edgePower,
    double dx_mm, double chWidth_mm, int channelCount)
{
    // 对齐你 MATLAB 的思路：只在 y 方向做“保边双线性”
    // 先计算相邻通道梯度 g(k,x)=|src[k+1,x]-src[k,x]|，用于自适应权重
    Image2D out;
    out.height = outH;
    out.width = srcW;
    out.data.resize(out.height * out.width);

    out.dx_mm = dx_mm;
    const double total_h_mm = chWidth_mm * std::max(0, channelCount - 1);
    out.dy_mm = (out.height > 1) ? (total_h_mm / double(out.height - 1)) : 0.0;

    if (srcH <= 0 || srcW <= 0 || outH <= 0) return out;
    if (srcH == 1) {
        // 只有一行，直接复制
        for (int oy = 0; oy < outH; ++oy)
            for (int x = 0; x < srcW; ++x)
                out.data[oy * out.width + x] = src[x];
        return out;
    }

    QVector<double> grad((srcH - 1) * srcW);
    double gmax = 0.0;
    for (int k = 0; k < srcH - 1; ++k) {
        for (int x = 0; x < srcW; ++x) {
            const double g = std::abs(src[(k + 1) * srcW + x] - src[k * srcW + x]);
            grad[k * srcW + x] = g;
            gmax = std::max(gmax, g);
        }
    }
    if (gmax <= 1e-12) gmax = 1.0;

    for (int oy = 0; oy < outH; ++oy) {
        const double y = (outH == 1) ? 0.0 : (double(oy) * (double(srcH - 1) / double(outH - 1)));

        if (y <= 0.0) {
            for (int x = 0; x < srcW; ++x)
                out.data[oy * out.width + x] = src[0 * srcW + x];
            continue;
        }
        if (y >= double(srcH - 1)) {
            for (int x = 0; x < srcW; ++x)
                out.data[oy * out.width + x] = src[(srcH - 1) * srcW + x];
            continue;
        }

        const int k = int(std::floor(y));
        const double d = y - k;      // in [0,1)

        for (int x = 0; x < srcW; ++x) {
            const double ga = grad[k * srcW + x] / gmax;

            double s = 1.0;
            if (ga >= edgeGateFrac) {
                s = 1.0 + sharpAlpha * std::pow(ga, edgePower);
            }

            // wa = d^s / (d^s + (1-d)^s)
            const double a = std::pow(d, s);
            const double b = std::pow(1.0 - d, s);
            const double w = (a + b > 1e-12) ? (a / (a + b)) : d;

            const double v0 = src[k * srcW + x];
            const double v1 = src[(k + 1) * srcW + x];
            out.data[oy * out.width + x] = (1.0 - w) * v0 + w * v1;
        }
    }

    return out;
}

// ======== Haar 简化融合（可选） ========
// 说明：此处做一个“多层：低频平均 + 高频取幅值更大者”的融合策略。
// 这是一个能跑通的替代方案，不等价 MATLAB 的 sym4。
static inline void haarDWT2(const Image2D& in, Image2D& LL, Image2D& LH, Image2D& HL, Image2D& HH)
{
    // 只处理偶数尺寸（简单起见），奇数则裁一行一列
    const int H = (in.height / 2) * 2;
    const int W = (in.width / 2) * 2;

    const int h2 = H / 2;
    const int w2 = W / 2;

    LL = Image2D{ h2, w2, QVector<double>(h2 * w2), in.dx_mm * 2.0, in.dy_mm * 2.0 };
    LH = Image2D{ h2, w2, QVector<double>(h2 * w2), in.dx_mm * 2.0, in.dy_mm * 2.0 };
    HL = Image2D{ h2, w2, QVector<double>(h2 * w2), in.dx_mm * 2.0, in.dy_mm * 2.0 };
    HH = Image2D{ h2, w2, QVector<double>(h2 * w2), in.dx_mm * 2.0, in.dy_mm * 2.0 };

    for (int y = 0; y < H; y += 2) {
        for (int x = 0; x < W; x += 2) {
            const double a = in.data[(y)*in.width + (x)];
            const double b = in.data[(y)*in.width + (x + 1)];
            const double c = in.data[(y + 1) * in.width + (x)];
            const double d = in.data[(y + 1) * in.width + (x + 1)];

            // Haar: 平均与差分
            const double avg = (a + b + c + d) * 0.25;
            const double lh = (a - b + c - d) * 0.25;
            const double hl = (a + b - c - d) * 0.25;
            const double hh = (a - b - c + d) * 0.25;

            const int oy = y / 2;
            const int ox = x / 2;
            const int idx = oy * w2 + ox;

            LL.data[idx] = avg;
            LH.data[idx] = lh;
            HL.data[idx] = hl;
            HH.data[idx] = hh;
        }
    }
}

static inline Image2D haarIDWT2(const Image2D& LL, const Image2D& LH, const Image2D& HL, const Image2D& HH, int outH, int outW)
{
    Image2D out;
    out.height = outH;
    out.width = outW;
    out.data.resize(outH * outW);
    out.dx_mm = LL.dx_mm * 0.5;
    out.dy_mm = LL.dy_mm * 0.5;

    const int h2 = LL.height;
    const int w2 = LL.width;

    for (int y = 0; y < h2; ++y) {
        for (int x = 0; x < w2; ++x) {
            const int idx = y * w2 + x;

            const double avg = LL.data[idx];
            const double lh = LH.data[idx];
            const double hl = HL.data[idx];
            const double hh = HH.data[idx];

            // 逆变换
            const double a = avg + lh + hl + hh;
            const double b = avg - lh + hl - hh;
            const double c = avg + lh - hl - hh;
            const double d = avg - lh - hl + hh;

            const int oy = 2 * y;
            const int ox = 2 * x;

            if (oy < outH && ox < outW) out.data[oy * outW + ox] = a;
            if (oy < outH && ox + 1 < outW) out.data[oy * outW + (ox + 1)] = b;
            if (oy + 1 < outH && ox < outW) out.data[(oy + 1) * outW + ox] = c;
            if (oy + 1 < outH && ox + 1 < outW) out.data[(oy + 1) * outW + (ox + 1)] = d;
        }
    }
    return out;
}

Image2D ImagingWorker::haarFusion(const Image2D& A, const Image2D& B, int levels)
{
    if (!A.isValid() || !B.isValid()) return Image2D{};
    if (A.height != B.height || A.width != B.width) return Image2D{};

    levels = std::max(1, levels);

    Image2D curA = A;
    Image2D curB = B;

    struct Bands { Image2D LH, HL, HH; int outH, outW; };
    QVector<Bands> stack;
    stack.reserve(levels);

    for (int lv = 0; lv < levels; ++lv) {
        if (curA.height < 2 || curA.width < 2) break;

        Image2D LLA, LHA, HLA, HHA;
        Image2D LLB, LHB, HLB, HHB;

        haarDWT2(curA, LLA, LHA, HLA, HHA);
        haarDWT2(curB, LLB, LHB, HLB, HHB);

        // 低频平均
        Image2D LL = LLA;
        for (int i = 0; i < LL.data.size(); ++i)
            LL.data[i] = 0.5 * (LLA.data[i] + LLB.data[i]);

        // 高频取幅值更大者（与你 MATLAB 取 abs 更大者一致）
        auto pickMaxAbs = [](const Image2D& X, const Image2D& Y) {
            Image2D Z = X;
            for (int i = 0; i < Z.data.size(); ++i) {
                if (std::abs(Y.data[i]) > std::abs(X.data[i])) Z.data[i] = Y.data[i];
            }
            return Z;
            };

        Bands bd;
        bd.LH = pickMaxAbs(LHA, LHB);
        bd.HL = pickMaxAbs(HLA, HLB);
        bd.HH = pickMaxAbs(HHA, HHB);
        bd.outH = curA.height;
        bd.outW = curA.width;
        stack.push_back(bd);

        curA = LL;
        curB = LL; // 下一层用融合后的低频继续
    }

    Image2D F = curA;
    for (int i = stack.size() - 1; i >= 0; --i) {
        F = haarIDWT2(F, stack[i].LH, stack[i].HL, stack[i].HH, stack[i].outH, stack[i].outW);
    }
    return F;
}

void ImagingWorker::onBuffer(const ScanBuffer& buf)
{
    m_stop.store(false);

    ImagingSettings s;
    {
        QMutexLocker lk(&m_mtx);
        s = m_settings;
    }

    if (!buf.isValid()) {
        emit imagingError("ScanBuffer invalid.");
        return;
    }

    const int srcH = buf.channelCount;
    const int srcW = buf.scanCount;

    // 组源矩阵（按行）
    QVector<double> ampSrc = buf.amp;
    QVector<double> phaSrc = buf.pha;

    // 输出高度：interpChannels（至少为 srcH）
    const int outH = std::max(srcH, s.interpChannels);

    // Step5 插值模式判定
    const QString m = s.interpMethod.toLower();
    const bool wantCubic = (m == "bicubic" || m == "cubic" || m == "spline"); // spline 先按 cubic 近似
    const bool wantEdgeAware = s.channelSharp && (m == "linear" || m == "bilinear");

    Image2D ampI, phaI;

    if (wantEdgeAware) {
        ampI = interpAlongChannelEdgeAware(
            ampSrc, srcH, srcW, outH,
            s.sharpAlphaAmp, s.edgeGateFrac, s.edgePower,
            s.dx_mm, s.chWidth_mm, srcH);

        phaI = interpAlongChannelEdgeAware(
            phaSrc, srcH, srcW, outH,
            s.sharpAlphaPha, s.edgeGateFrac, s.edgePower,
            s.dx_mm, s.chWidth_mm, srcH);
    }
    else {
        ampI = interpAlongChannel(ampSrc, srcH, srcW, outH, wantCubic, s.dx_mm, s.chWidth_mm, srcH);
        phaI = interpAlongChannel(phaSrc, srcH, srcW, outH, wantCubic, s.dx_mm, s.chWidth_mm, srcH);
    }

    if (m_stop.load()) return;

    ImagingResult r;
    r.jobSeq = m_jobSeq++;
    r.startTimeNs = buf.startTimeNs;
    r.endTimeNs = buf.endTimeNs;

    smoothX_121(ampI);
    r.ampInterp = ampI;
    r.phaInterp = phaI;

    if (s.enableWavelet) {
        Image2D F = haarFusion(ampI, phaI, s.waveletLevels);

        // ✅ 轻微平滑（仅x方向），抑制静止抖动
        // 注意：放在归一化之前/之后都行；我建议放在归一化之前
        if (F.isValid()) {
            smoothX_121(F);
        }

        // 归一化到 0..1（类似你 MATLAB 的 Fs）
        if (F.isValid()) {
            double mn = F.data[0], mx = F.data[0];
            for (double v : F.data) { mn = std::min(mn, v); mx = std::max(mx, v); }
            const double den = (mx - mn <= 1e-12) ? 1.0 : (mx - mn);
            for (double& v : F.data) v = (v - mn) / den;

            r.hasFused = true;
            r.fusedNorm = F;
        }
    }

    emit resultReady(r);
}
