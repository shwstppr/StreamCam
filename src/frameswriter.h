/* Copyright (c) 2015 Abhishek Kumar
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FRAMESWRITER_H_
#define FRAMESWRITER_H_

#include <QObject>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QLinkedList>
#include <QMutex>
#include <QWaitCondition>
#include "mediaframe.h"

#define VERBOSE false
#define MAX_QUEUE_SIZE 32

class FramesWriter : public QObject
{
    Q_OBJECT
public:
    FramesWriter(QString path = QString(),
            QObject* parent = 0);
    ~FramesWriter();
    void run();
    void postFrame(MediaFrame frame);

signals:
    void writeLocationError();
    void finished();

public slots:
    void start();
    void safeStop();

private slots:

private:
    bool writeAudioFrame(const long timestamp, const QByteArray data);
    bool writeVideoFrame(const long timestamp, const QByteArray data);

    QString mWriteLocation;
    QMutex* mLock;
    QWaitCondition mCondition;
    bool mIsStopped;
    QLinkedList<MediaFrame> mAudioQueue;
    QLinkedList<MediaFrame> mVideoQueue;
    int mAudioFramesReceivedCount;
    int mVideoFramesReceivedCount;
    int mDroppedFramesCount;
};

#endif /* FRAMESWRITER_H_ */
