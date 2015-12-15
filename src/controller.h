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

#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <QObject>
#include <QTimer>
#include "rtmppublisher.h"
#include "frameswriter.h"
#include <stdint.h>

#define FRAMESWRITER_ENABLED false
#define KEY_SERVER_URL "Server_Url"

class Controller : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isStreaming READ isStreaming NOTIFY isStreamingChanged)
    Q_PROPERTY(QString serverDisplay READ serverDisplay NOTIFY serverDisplayChanged)
    Q_PROPERTY(QString audioBitrate READ audioBitrate NOTIFY audioBitrateChanged)
    Q_PROPERTY(QString audioSamplingRate READ audioSamplingRate NOTIFY audioSamplingRateChanged)
    Q_PROPERTY(QString audioChannel READ audioChannel NOTIFY audioChannelChanged)
    Q_PROPERTY(int droppedFramesCount READ droppedFramesCount NOTIFY droppedFramesCountChanged)
    Q_PROPERTY(int totalFramesCount READ totalFramesCount NOTIFY totalFramesCountChanged)
    Q_PROPERTY(int audioFramesCount READ audioFramesCount NOTIFY audioFramesCountChanged)
    Q_PROPERTY(int videoFramesCount READ videoFramesCount NOTIFY videoFramesCountChanged)

public:
    Controller(QObject* parent = 0);
    ~Controller();

    void setHost(const QString host) { mHost = host; emit hostChanged(); }
    void setPort(const int port) { mPort = port; emit portChanged(); }
    void setApp(const QString app) { mApp = app; emit appChanged(); }
    void setPlayPath(const QString playPath) { mPlayPath = playPath; emit playPathChanged(); }
    void setIsStreaming(const bool isStreaming) { mIsStreaming = isStreaming; emit isStreamingChanged(); }
    void setAudioBitrate(const QString audioBitrate) { mAudioBitrate = audioBitrate; emit audioBitrateChanged(); }
    void setAudioSamplingRate(const QString audioSamplingRate) { mAudioSamplingRate = audioSamplingRate; emit audioSamplingRateChanged(); }
    void setAudioChannel(const QString audioChannel) { mAudioChannel = audioChannel; emit audioChannelChanged(); }

    QString host() { return mHost; }
    int port() { return mPort; }
    QString app() { return mApp; }
    QString playPath() { return mPlayPath; }
    QString serverDisplay();
    bool isStreaming() { return mIsStreaming; }

    QString audioBitrate() { return mAudioBitrate; }
    QString audioSamplingRate() { return mAudioSamplingRate; }
    QString audioChannel() { return mAudioChannel; }

    int droppedFramesCount();
    int totalFramesCount();
    int audioFramesCount();
    int videoFramesCount();
    qint64 totalBytesSent();

public slots:
    void startStreaming();
    void stopStreaming();
    void handleAudioFrame(const uint8_t* frameBuffer,
            const uint64_t frameSize,
            const uint64_t timestamp,
            const bool isKeyFrame);
    void handleVideoFrame(const uint8_t* frameBuffer,
            const uint64_t frameSize,
            const uint64_t timestamp,
            const bool isKeyFrame);
    bool setServer(QString serverUrl, bool doSave = true);


private slots:
    void on_mRTMPPublisher_socketError(const int error);
    void on_mRTMPPublisher_finished();
    void on_mFramesWriter_finished();

signals:
    void hostChanged();
    void portChanged();
    void appChanged();
    void playPathChanged();
    void serverDisplayChanged();
    void isStreamingChanged();
    void audioBitrateChanged();
    void audioSamplingRateChanged();
    void audioChannelChanged();
    void audioFramesCountChanged();
    void videoFramesCountChanged();
    void droppedFramesCountChanged();
    void totalFramesCountChanged();

    void publishError(QString error);

private:
    void clearVars();
    QString mHost;
    int mPort;
    QString mApp;
    QString mPlayPath;
    bool mIsStreaming;
    int mVideoFileDescriptor;
    uint64_t mAudioStartTS;
    long mLastAudioTS;
    int mRemainingAudioTS;
    bool mIsAACHeaderSent;
    uint64_t mVideoStartTS;
    long mLastVideoTS;
    int mRemainingVideoTS;
    bool mIsSPSSent;
    bool mIsPPSSent;
    int mAudioFrameCount;
    int mVideoFrameCount;
    QByteArray mVideoSPS;
    QByteArray mVideoPPS;
    long mTotalBytesDecoded;
    RTMPPublisher* mRTMPPublisher;
#if(FRAMESWRITER_ENABLED)
    FramesWriter* mFramesWriter;
#endif

    //For QML
    QString mAudioBitrate;
    QString mAudioSamplingRate;
    QString mAudioChannel;
};

#endif /* CONTROLLER_H_ */
