#include "SaveWorker.h"
#include <QDateTime>

bool SaveWorker::openRaw(const QString& path, int baud, int points, int channels)
{
    closeRaw();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(m_file.errorString()); return false;
    }
    m_ds.setDevice(&m_file);
    m_ds.resetStatus();
    m_ds.setByteOrder(QDataStream::LittleEndian);
    m_chunks = m_bytes = 0;
    m_ds.writeRawData("RAW1", 4);
    m_ds << quint32(1) << quint32(88);
    m_ds << qint64(QDateTime::currentMSecsSinceEpoch() * 1000000LL);
    m_endOffset = m_file.pos(); m_ds << qint64(0);
    m_ds << quint32(baud) << quint32(points) << quint32(channels);
    m_chunksOffset = m_file.pos(); m_ds << quint64(0);
    m_bytesOffset = m_file.pos(); m_ds << quint64(0);
    const QByteArray reserved(32, 0);
    m_ds.writeRawData(reserved.constData(), reserved.size());
    if (m_ds.status() != QDataStream::Ok) {
        emit failed(QStringLiteral("RAW1 header write failed"));
        m_file.close(); return false;
    }
    return true;
}

void SaveWorker::enqueueRaw(const QByteArray& bytes)
{
    const qint64 n = bytes.size();
    if (m_pendingBytes.fetch_add(n) + n > 8 * 1024 * 1024) {
        m_pendingBytes.fetch_sub(n);
        emit failed(QStringLiteral("Raw save backlog exceeded 8 MiB; stop capture and check disk"));
        return;
    }
    const bool posted = QMetaObject::invokeMethod(this, [this, bytes, n]() {
        appendRaw(bytes);
        m_pendingBytes.fetch_sub(n);
    }, Qt::QueuedConnection);
    if (!posted) { m_pendingBytes.fetch_sub(n); emit failed(QStringLiteral("Cannot queue raw save")); }
}

void SaveWorker::appendRaw(const QByteArray& bytes)
{
    if (!m_file.isOpen()) return;
    m_ds << quint32(bytes.size());
    const int n = m_ds.writeRawData(bytes.constData(), bytes.size());
    if (n != bytes.size() || m_ds.status() != QDataStream::Ok) {
        emit failed(QStringLiteral("Raw data write failed"));
        m_file.close(); return;
    }
    ++m_chunks; m_bytes += quint64(bytes.size());
}

void SaveWorker::closeRaw()
{
    if (!m_file.isOpen()) return;
    bool ok = m_file.seek(m_endOffset);
    m_ds << qint64(QDateTime::currentMSecsSinceEpoch() * 1000000LL);
    ok = m_file.seek(m_chunksOffset) && ok; m_ds << m_chunks;
    ok = m_file.seek(m_bytesOffset) && ok; m_ds << m_bytes;
    ok = m_file.flush() && ok;
    if (!ok || m_ds.status() != QDataStream::Ok) emit failed(QStringLiteral("Raw finalization failed"));
    m_file.close();
    emit rawClosed(m_chunks, m_bytes);
}

static void writeImage(QDataStream& ds, const Image2D& im)
{
    ds << qint32(im.width) << qint32(im.height) << quint32(im.data.size());
    for (double v : im.data) ds << v;
}

void SaveWorker::enqueueImage(const QString& base, const QImage& image, const ImagingResult& result)
{
    if (m_pendingImages.fetch_add(1) >= 4) {
        m_pendingImages.fetch_sub(1);
        emit failed(QStringLiteral("Image save backlog exceeded 4 jobs")); return;
    }
    const bool posted = QMetaObject::invokeMethod(this, [this, base, image, result]() {
        bool ok = image.isNull() || image.save(base + "_show.png", "PNG");
        QFile f(base + "_img.bin");
        if (f.open(QIODevice::WriteOnly)) {
            QDataStream ds(&f);
            ds.setByteOrder(QDataStream::LittleEndian);
            ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
            ds.writeRawData("IMG1", 4);
            ds << quint32(1) << quint64(result.jobSeq) << qint64(result.startTimeNs) << qint64(result.endTimeNs);
            writeImage(ds, result.ampInterp); writeImage(ds, result.phaInterp);
            ds << quint8(result.hasFused ? 1 : 0);
            if (result.hasFused) writeImage(ds, result.fusedNorm);
            ok = (ds.status() == QDataStream::Ok && f.flush()) && ok;
        } else ok = false;
        if (!ok) emit failed(QStringLiteral("Image save failed: ") + base);
        m_pendingImages.fetch_sub(1);
    }, Qt::QueuedConnection);
    if (!posted) { m_pendingImages.fetch_sub(1); emit failed(QStringLiteral("Cannot queue image save")); }
}
