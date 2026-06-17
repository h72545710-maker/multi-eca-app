#include "ChartsController.h"
#include <QComboBox>
#include <QLabel>

ChartsController::ChartsController(QObject* parent)
    : QObject(parent)
    , m_amp1(this), m_pha1(this), m_amp2(this), m_pha2(this)
{
}

void ChartsController::bindUi(QWidget* wAmp1, QComboBox* cbAmp1,
    QWidget* wPha1, QComboBox* cbPha1,
    QWidget* wAmp2, QComboBox* cbAmp2,
    QWidget* wPha2, QComboBox* cbPha2
)
{
    m_amp1.bind(wAmp1, cbAmp1, ChartPanel::Kind::Amp,
        "Amplitude Signal (Panel 1)");

    m_pha1.bind(wPha1, cbPha1, ChartPanel::Kind::Pha,
        "Phase Signal (Panel 1)");

    m_amp2.bind(wAmp2, cbAmp2, ChartPanel::Kind::Amp,
        "Amplitude Signal (Panel 2)");

    m_pha2.bind(wPha2, cbPha2, ChartPanel::Kind::Pha,
        "Phase Signal (Panel 2)");

    setChannelCount(m_chCount);
    setXPoints(m_xPoints);
}

void ChartsController::setChannelCount(int n)
{
    m_chCount = (n <= 0) ? 1 : n;

    m_amp1.setChannelCount(m_chCount);
    m_pha1.setChannelCount(m_chCount);
    m_amp2.setChannelCount(m_chCount);
    m_pha2.setChannelCount(m_chCount);

    ensureBuffers();
}

void ChartsController::setXPoints(int points)
{
    m_xPoints = (points <= 1) ? 2 : points;

    m_amp1.setXRange(m_xPoints);
    m_pha1.setXRange(m_xPoints);
    m_amp2.setXRange(m_xPoints);
    m_pha2.setXRange(m_xPoints);
}

void ChartsController::ensureBuffers()
{
    m_ampBuf.resize(m_chCount);
    m_phaBuf.resize(m_chCount);
}

void ChartsController::pushRing(QVector<double>& v, double x, int maxN)
{
    v.push_back(x);
    if (v.size() > maxN) v.remove(0, v.size() - maxN);
}

void ChartsController::pushSample(int ch, double ampV, double phaV)
{
    if (ch < 0 || ch >= m_chCount) return;
    ensureBuffers();

    pushRing(m_ampBuf[ch], ampV, m_xPoints);
    pushRing(m_phaBuf[ch], phaV, m_xPoints);

    // 右侧状态：显示“最近一次采样”
    if (m_labelStatus) {
        m_labelStatus->setText(QString("CH %1 | Amp= %2 V | Pha= %3 V")
            .arg(ch)
            .arg(ampV, 0, 'f', 6)
            .arg(phaV, 0, 'f', 6));
    }
}

void ChartsController::refresh()
{
    ensureBuffers();

    const int chAmp1 = m_amp1.currentChannel();
    const int chPha1 = m_pha1.currentChannel();
    const int chAmp2 = m_amp2.currentChannel();
    const int chPha2 = m_pha2.currentChannel();

    if (chAmp1 >= 0 && chAmp1 < m_chCount) m_amp1.updateSeries(m_ampBuf[chAmp1]);
    if (chPha1 >= 0 && chPha1 < m_chCount) m_pha1.updateSeries(m_phaBuf[chPha1]);
    if (chAmp2 >= 0 && chAmp2 < m_chCount) m_amp2.updateSeries(m_ampBuf[chAmp2]);
    if (chPha2 >= 0 && chPha2 < m_chCount) m_pha2.updateSeries(m_phaBuf[chPha2]);
}
