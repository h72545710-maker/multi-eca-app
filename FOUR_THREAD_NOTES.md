# Four-thread acquisition application

This branch implements four application threads, including the GUI thread:

| Thread | Work |
| --- | --- |
| GUI | Controls, channel buffering, curve refresh and final curve/image widget updates |
| Serial | QSerialPort ownership, readyRead and seven-byte frame parsing |
| Imaging | Interpolation/fusion; queued results return to GUI |
| Save | RAW1 stream writes, image BIN/PNG writes and file finalization |

There are three QThread objects plus QApplication's main thread. QWidget and QPixmap are never operated on by a background thread. PNG jobs receive QImage snapshots. A Worker class alone does not imply a separate thread.

## Lifetime and ordering

- SerialWorker has no GUI parent and moves before QSerialPort is created. Open/close are dispatched to its thread; status arrives asynchronously.
- RawFrame is declared and registered as a Qt metatype before queued delivery.
- DataProcessWorker stays in the GUI thread so existing control setters do not become unsafe cross-thread calls.
- SaveWorker owns its QFile as a QObject child, so the file moves to the save thread with its owner.
- Normal save jobs are asynchronous. Start/stop use a short blocking queued control operation to order open, queued writes and finalization. A stalled disk can still delay these barriers.
- Raw save backlog is bounded at 8 MiB and pending image jobs at four. Overflow/write failure is reported and disables saving; this is observable failure handling, not a zero-loss guarantee or firmware flow control.
- Shutdown closes serial in its owner thread, stops acquisition of new UI jobs, drains previously queued saves, closes files, and waits for worker threads before destruction.
- The existing RAW1 88-byte header and chunk records and IMG1 layout are preserved. Locally generated rxSeq is not a firmware sequence counter and cannot prove no frames were lost before parsing.

## Validation status

The VS project XML parses and the RAW1 reference header layout was checked. Full MSBuild/Qt compilation and runtime tests were not possible in the authoring environment. Existing external QCustomPlot paths remain required.

Before merging: build in the original VS/Qt environment, verify thread IDs, replay split/multiple frames, capture a raw file and decode it with the existing analysis code, test image saving, repeated open/close and window shutdown during reception. Test disk errors/backlog reporting. Compare UI behavior and decoding against main.
