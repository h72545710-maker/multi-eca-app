#pragma once
#include <QObject>
#include <QFile>
#include <QDataStream>
#include <QImage>
#include <QMetaObject>
#include <atomic>
#include "ImagingResult.h"

// All file operations run in the save thread. enqueue* may be called by GUI.
class SaveWorker : public QObject {
    Q_OBJECT
public:
    explicit SaveWorker(QObject* parent = nullptr) : QObject(parent) { m_file.setParent(this); }
    bool openRaw(const QString& path, int baud, int points, int channels);
    void closeRaw();
    void enqueueRaw(const QByteArray& bytes);
    void enqueueImage(const QString& base, const QImage& image, const ImagingResult& result);
signals:
    void failed(const QString& message);
    void rawClosed(quint64 chunks, quint64 bytes);
private:
    void appendRaw(const QByteArray& bytes);
    QFile m_file;
    QDataStream m_ds;
    quint64 m_chunks = 0, m_bytes = 0;
    qint64 m_endOffset = -1, m_chunksOffset = -1, m_bytesOffset = -1;
    std::atomic<qint64> m_pendingBytes{0};
    std::atomic<int> m_pendingImages{0};
};
