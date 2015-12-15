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

#ifndef RTMPPUBLISHER_H
#define RTMPPUBLISHER_H

#include <QObject>
#include <QThread>
#include <QtNetwork/QTcpSocket>
#include <QLinkedList>
#include <QMutex>
#include <QWaitCondition>
#include <QTime>
#include "mediaframe.h"

#define CHUNK_SIZE 4096
#define VERBOSE false
#define MAX_QUEUE_SIZE 128
#define LOG_HIGH_WRITE_TIMES false
#define HIGH_WRITE_LIMIT_MSEC 10

class RTMPPublisher : public QObject
{
    Q_OBJECT
public:

    RTMPPublisher(QString host,
            int port,
            QString app,
            QString path,
            QObject* parent = 0);
    ~RTMPPublisher();
    void run();
    bool initiate();
    void setAudioHeader(QByteArray header, int nchan, int srate, int ssize);
    void postFrame(MediaFrame frame);
    static inline QByteArray IntToArray(qint32 source);

    int droppedFramesCount() { return mDroppedFramesCount; }
    int totalFramesCount() { return mAudioFramesReceivedCount+mVideoFramesReceivedCount; }
    int audioFramesCount() { return mAudioFramesReceivedCount; }
    int videoFramesCount() { return mVideoFramesReceivedCount; }
    qint64 totalBytesWritten() { return mTotalBytesWritten; }

signals:
    void socketError(int error);
    void finished();
    void audioFramesCountChanged();
    void videoFramesCountChanged();
    void droppedFramesCountChanged();

public slots:
    void start();
    void safeStop();

private slots:
    void on_mSocket_readyRead();
    void on_mSocket_error(QAbstractSocket::SocketError socketError);
    void on_mSocket_bytesWritten(qint64 bytes);
private:
    void connect();
    void handshake();
    void publish();
    void sendAudioFrame(QByteArray frame, long ts);
    void sendVideoNal(QByteArray nal, long pts, long dts);
    void setChunkSize();
    void startAudio(long ts);
    void startVideo(long ts);
    void destroySocket();
    bool isSocketConnected();
    bool waitForReadyRead(int msecs = 30000);
    QByteArray readAll();
    qint64 write(const QByteArray data, const bool doWait = true);
    qint64 write(const char* data, qint64 length, const bool doWait = true);
    qint64 write(const unsigned char* data, qint64 length, const bool doWait = true);
    qint64 write(const unsigned char data, const bool doWait = true);
    qint64 write(const qint32 data, const bool doWait = true);
    QTcpSocket *mSocket;
    QMutex* mLock;
    QWaitCondition mCondition;
    QString mHost;
    int mPort;
    QString mApp;
    QString mPlayPath;
    QByteArray mAACHeader;
    int mAACFormat;
    bool mHasAudio;
    long mAudioTimestamp;
    bool mHasVideo;
    QByteArray mSPS;
    QByteArray mPPS;
    long mVideoTimestamp;
    int mNumChannels;
    int mSampleRate;
    int mSampleSize;
    QLinkedList<MediaFrame> mAudioQueue;
    QLinkedList<MediaFrame> mVideoQueue;
    bool mIsWaitingForHandshakeReadyRead;
    bool mIsStopped;
    int mAudioFramesReceivedCount;
    int mVideoFramesReceivedCount;
    int mDroppedFramesCount;
    long mLastReceivedFrameTS;
    qint64 mTotalBytesWritten;

#if (LOG_HIGH_WRITE_TIMES)
    QTime logTimer;
#endif
};

#endif //RTMPPUBLISHER_H
