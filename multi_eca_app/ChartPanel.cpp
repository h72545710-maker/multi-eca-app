#include "ChartPanel.h"

#include <QWidget>
#include <QComboBox>
#include <QVBoxLayout>
#include <QGraphicsLayout>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

ChartPanel::ChartPanel(QObject* parent) : QObject(parent) {}

void ChartPanel::bind(QWidget* container, QComboBox* combo,
    Kind kind, const QString& title)
{
    m_container = container;
    m_combo = combo;
    m_kind = kind;
    m_title = title;

    ensureChartBuilt();
    rebuildComboItems();

    // 连接下拉框
    QObject::connect(m_combo, &QComboBox::currentIndexChanged,
        this, &ChartPanel::onComboChanged);
}

void ChartPanel::setChannelCount(int n)
{
    m_channelCount = (n <= 0) ? 1 : n;
    if (m_currentCh >= m_channelCount) m_currentCh = 0;
    rebuildComboItems();
}

void ChartPanel::setCurrentChannel(int ch)
{
    if (ch < 0) ch = 0;
    if (ch >= m_channelCount) ch = m_channelCount - 1;
    m_currentCh = ch;

    if (m_combo && m_combo->currentIndex() != ch) {
        m_combo->setCurrentIndex(ch);
    }
}

void ChartPanel::setXRange(int points)
{
    m_xPoints = (points <= 1) ? 2 : points;
    if (m_axisX) m_axisX->setRange(0, m_xPoints - 1);
}

void ChartPanel::updateSeries(const QVector<double>& y)
{
    if (!m_series) return;
    m_series->clear();

    const int N = y.size();
    if (N <= 0) return;

    // 更新 X 轴范围
    if (m_axisX) m_axisX->setRange(0, N - 1);

    // 计算 Y 轴范围（自动缩放，留一点边界）
    double ymin = y[0], ymax = y[0];
    for (double v : y) { ymin = std::min(ymin, v); ymax = std::max(ymax, v); }
    if (std::abs(ymax - ymin) < 1e-12) { ymax = ymin + 1.0; }
    const double pad = 0.05 * (ymax - ymin);
    if (m_axisY) m_axisY->setRange(ymin - pad, ymax + pad);

    for (int i = 0; i < N; ++i) {
        m_series->append(i, y[i]);
    }
}

void ChartPanel::onComboChanged(int index)
{
    if (index < 0) return;
    m_currentCh = index;
    emit channelChanged(index);
}

void ChartPanel::ensureChartBuilt()
{
    if (!m_container) return;

    // container 必须有 layout，否则 ChartView 放不进去
    if (!m_container->layout()) {
        auto* lay = new QVBoxLayout(m_container);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        m_container->setLayout(lay);
    }

    // 清空旧内容（稳一点）
    while (auto* item = m_container->layout()->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    m_series = new QLineSeries(this);
    m_chart = new QChart();
    m_chart->addSeries(m_series);
    m_chart->setTitle(m_title);

    // 去边距：更像你截图里的“干净图”
    m_chart->layout()->setContentsMargins(0, 0, 0, 0);
    m_chart->setMargins(QMargins(0, 0, 0, 0));

    m_axisX = new QValueAxis();
    m_axisX->setTitleText("sample");
    m_axisX->setRange(0, m_xPoints - 1);

    m_axisY = new QValueAxis();
    m_axisY->setTitleText("V");

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_series->attachAxis(m_axisX);
    m_series->attachAxis(m_axisY);

    m_view = new QChartView(m_chart, m_container);
    m_view->setRenderHint(QPainter::Antialiasing, true);

    m_container->layout()->addWidget(m_view);
}

void ChartPanel::rebuildComboItems()
{
    if (!m_combo) return;

    const bool blocked = m_combo->blockSignals(true);
    m_combo->clear();
    for (int ch = 0; ch < m_channelCount; ++ch) {
        m_combo->addItem(QString("CH %1").arg(ch), ch);
    }
    m_combo->setCurrentIndex(m_currentCh);
    m_combo->blockSignals(blocked);
}
