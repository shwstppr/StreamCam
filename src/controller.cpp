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

#include "controller.h"
#include <QUrl>
#include <QSettings>

Controller::Controller(QObject* parent)
    :QObject(parent) {
    mRTMPPublisher = NULL;
#if(FRAMESWRITER_ENABLED)
    mFramesWriter = NULL;
#endif
    setIsStreaming(false);
    connect(this,SIGNAL(hostChanged()),this,SIGNAL(serverDisplayChanged()));
    connect(this,SIGNAL(portChanged()),this,SIGNAL(serverDisplayChanged()));
    connect(this,SIGNAL(appChanged()),this,SIGNAL(serverDisplayChanged()));
    connect(this,SIGNAL(playPathChanged()),this,SIGNAL(serverDisplayChanged()));
}

Controller::~Controller()
{
    if(mRTMPPublisher!=NULL) {
        mRTMPPublisher->safeStop();
    }
#if(FRAMESWRITER_ENABLED)
    if(mFramesWriter!=NULL) {
        mFramesWriter->safeStop();
    }
#endif
}

void Controller::on_mRTMPPublisher_finished()
{
    qDebug()<<"Delete mRTMPPublisher!";
    mRTMPPublisher = NULL;
}

void Controller::on_mRTMPPublisher_socketError(const int error) {
    qDebug()<<"Controller-SocketError"<<error;
    QAbstractSocket::SocketError err = (QAbstractSocket::SocketError)error;
    QString errorString = tr("Socket error!");
    if(err == QAbstractSocket::HostNotFoundError)
        errorString += tr(" %1 was not found.").arg(mHost);
    else if(err == QAbstractSocket::SocketAccessError)
        errorString += tr(" The socket operation failed because the application lacked the required privileges.");
    else if(err == QAbstractSocket::RemoteHostClosedError)
        errorString += tr(" %1 closed the connection.").arg(mHost);
    else if(err == QAbstractSocket::ConnectionRefusedError)
        errorString += tr(" %1 refused connection.").arg(mHost);
    setIsStreaming(false);
    emit publishError(errorString);
}

void Controller::on_mFramesWriter_finished() {
    qDebug()<<"Delete mFramesWriter!";
#if(FRAMESWRITER_ENABLED)
    mFramesWriter = NULL;
#endif
}

void Controller::clearVars()
{
    mAudioStartTS = 0;
    mLastAudioTS = 0;
    mRemainingAudioTS = 0;
    mIsAACHeaderSent = false;
    mVideoStartTS = 0;
    mLastVideoTS = 0;
    mRemainingVideoTS = 0;
    mIsSPSSent = false;
    mIsPPSSent = false;
    mVideoSPS.clear();
    mVideoPPS.clear();
    mAudioFrameCount = 0;
    mVideoFrameCount = 0;
    mTotalBytesDecoded = 0;
    setAudioBitrate("");
    setAudioSamplingRate("");
    setAudioChannel("");
}

void Controller::startStreaming()
{
    if(mRTMPPublisher==NULL && !mIsStreaming) {
        setIsStreaming(true);
        clearVars();
        QThread* thread = new QThread();
//        if(mHost.isEmpty())
//            setHost("a.rtmp.youtube.com");
//        setPort(1935);
//        if(mApp.isEmpty())
//            setApp("live2");
//        if(mPlayPath.isEmpty())
//            setPlayPath("abhishek");
        mRTMPPublisher = new RTMPPublisher(mHost, mPort, mApp, mPlayPath);
        connect(thread,SIGNAL(started()),mRTMPPublisher,SLOT(start()));
        connect(mRTMPPublisher,SIGNAL(socketError(int)),this,SLOT(on_mRTMPPublisher_socketError(int)));
        connect(mRTMPPublisher,SIGNAL(socketError(int)),thread,SLOT(quit()));
        connect(mRTMPPublisher,SIGNAL(finished()),thread,SLOT(quit()));
        connect(mRTMPPublisher,SIGNAL(audioFramesCountChanged()),this,SIGNAL(audioFramesCountChanged()));
        connect(mRTMPPublisher,SIGNAL(videoFramesCountChanged()),this,SIGNAL(videoFramesCountChanged()));
        connect(mRTMPPublisher,SIGNAL(audioFramesCountChanged()),this,SIGNAL(totalFramesCountChanged()));
        connect(mRTMPPublisher,SIGNAL(videoFramesCountChanged()),this,SIGNAL(totalFramesCountChanged()));
        connect(mRTMPPublisher,SIGNAL(droppedFramesCountChanged()),this,SIGNAL(droppedFramesCountChanged()));
        connect(thread,SIGNAL(finished()),mRTMPPublisher,SLOT(deleteLater()));
        connect(thread,SIGNAL(finished()),this,SLOT(on_mRTMPPublisher_finished()));
        connect(thread,SIGNAL(finished()),thread,SLOT(deleteLater()));
        mRTMPPublisher->moveToThread(thread);
        thread->start();

#if(FRAMESWRITER_ENABLED)
        if(mFramesWriter==NULL) {
            QThread* thread = new QThread();
            mFramesWriter = new FramesWriter("shared/documents/"+QDateTime::currentDateTime().toString("dd-MMM-yy hh:mm:ssAP"));
            connect(thread,SIGNAL(started()),mFramesWriter,SLOT(start()));
            connect(mFramesWriter,SIGNAL(finished()),thread,SLOT(quit()));
            connect(thread,SIGNAL(finished()),mFramesWriter,SLOT(deleteLater()));
            connect(thread,SIGNAL(finished()),this,SLOT(on_mFramesWriter_finished()));
            connect(thread,SIGNAL(finished()),thread,SLOT(deleteLater()));
            mFramesWriter->moveToThread(thread);
            thread->start();
        }
#endif
    }
}

void Controller::stopStreaming()
{
    if(mRTMPPublisher!=NULL &&  mIsStreaming) {
        if(this->mLastVideoTS>1000) {
            qDebug()<<(this->mAudioFrameCount+this->mVideoFrameCount)
                    <<"frames decoded"
                    <<"in"
                    <<(this->mLastVideoTS/1000)
                    <<"at"
                    <<(this->mAudioFrameCount+this->mVideoFrameCount)/(this->mLastVideoTS/1000)
                    <<"of"
                    <<mTotalBytesDecoded/1024<<"kb";
        }
        mRTMPPublisher->safeStop();
        setIsStreaming(false);
    }
}

void Controller::handleAudioFrame(const uint8_t* frameBuffer,
            const uint64_t frameSize,
            const uint64_t timestamp,
            const bool isKeyFrame)
{
    if(this->mAudioStartTS==0) {
        this->mAudioStartTS = timestamp;
        this->mLastAudioTS = 0;

        uchar iHeader[7];
        memcpy(iHeader, frameBuffer, 7);
        QByteArray k = QByteArray::fromRawData((char*)iHeader, 7);
        for(int m=0;m<k.size();m++)
            qDebug()<<(uint8_t)k.at(m);
        qDebug()<<"First loop done!"<<iHeader<<k;
        unsigned char firstByte = iHeader[0];
        if (!((firstByte & 255) == 255 && (firstByte & 240) == 240)) {
            qDebug()<<"Bad ADTS signature"<<(firstByte & 240)<<(int)firstByte<<iHeader[0];
            qDebug()<<"Audio signature error";
            return;
        }
        int size = (((iHeader[3] & 3) << 11) | ((iHeader[4] & 255) << 3)) | ((iHeader[5] & 224) >> 5);
        if (size < 7) {
            qDebug()<<"Bad ADTS packet size:"<<size;
            qDebug()<<"Audio packet size error";
            return;
        }

        int aot = ((iHeader[2] & 192) >> 6) + 1;
        int nof = (iHeader[6] & 3) + 1;
        int samplingRateIndex = (iHeader[2] & 60) >> 2;
        int channelCount = ((iHeader[2] & 1) << 2) | ((iHeader[3] & 192) >> 6);
        int iArrLength = 13;
        int iArr[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
        if (samplingRateIndex < 0 || samplingRateIndex >= iArrLength) {
//            break;
        }
        int samplingRate = iArr[samplingRateIndex];
        setAudioSamplingRate(QString("%1 kHz").arg((float)samplingRate/1000.0));
        if(channelCount==1)
            setAudioChannel("mono");
        else
            setAudioChannel("stereo");
        setAudioBitrate("194 kbps"); //TODO: Actually calculate this
        qDebug()<<QString("AAC aot=%1; freq=%2(%3); chan=%4; nof=%5").arg(aot).arg(samplingRateIndex).arg(samplingRate).arg(channelCount).arg(nof);
        uchar header[2] = {(uchar) ((samplingRateIndex >> 1) | 16), (uchar) ((samplingRateIndex << 7) | (channelCount << 3))};
        QByteArray h = QByteArray::fromRawData((char*)header, 2);
        qDebug()<<"Header"<<header<<h;
        if(!mIsAACHeaderSent && mRTMPPublisher!=NULL)
            mRTMPPublisher->setAudioHeader(h, channelCount, samplingRateIndex, 2);
        else
            qDebug()<<"RTMPPublisher is NULL! Audio!";
    }
    QByteArray buffer(reinterpret_cast<const char*>(frameBuffer), frameSize);
    buffer.remove(0,7);  //Remove AAC header
    long ts = (timestamp-this->mAudioStartTS);
    mRemainingAudioTS += ts%1000;
    ts /= 1000;
    if(mRemainingAudioTS>=1000) {
        ts += 1;
        mRemainingAudioTS -= 1000;
    }
    mAudioFrameCount++;
    if(VERBOSE)
        qDebug()<<"AudioFrames"<<mAudioFrameCount<<isKeyFrame<<ts<<buffer.size();
    if(mRTMPPublisher!=NULL) {
        MediaFrame frame;
        frame.buffer = buffer;
        frame.dts = ts;
        frame.pts = ts;
        frame.type = MediaFrame::AUDIO;
        mTotalBytesDecoded += frame.buffer.size();
        mRTMPPublisher->postFrame(frame);
#if(FRAMESWRITER_ENABLED)
        if(mFramesWriter!=NULL)
            mFramesWriter->postFrame(frame);
#endif
    } else
        qDebug()<<"RTMPPublisher is NULL! Audio!";
    this->mLastAudioTS = ts;
}

void Controller::handleVideoFrame(const uint8_t* frameBuffer,
            const uint64_t frameSize,
            const uint64_t timestamp,
            const bool isKeyFrame)
{
    QByteArray buffer(reinterpret_cast<const char*>(frameBuffer), frameSize);
    int prefixSize = 4;
    MediaFrame frame;
    frame.type = MediaFrame::VIDEO;
    int type;
    if(this->mVideoStartTS==0) {
        this->mVideoStartTS = timestamp;
        this->mLastVideoTS = 0;
        for(int i=4;i<frameSize && i<40;i++) {
            if(i+3<frameSize) {
                if(((uint8_t)buffer.at(i))==0 &&
                        ((uint8_t)buffer.at(i+1))==0 &&
                        ((uint8_t)buffer.at(i+2))==0 &&
                        ((uint8_t)buffer.at(i+3))==1) {
                    //0001 is H264, In AVC this will be size of NAL
                    //Moving to AVC might clean this code?
                    if(mVideoSPS.isEmpty()) {
                        mVideoSPS = buffer.left(i);
                    }
                    if(mVideoPPS.isEmpty()) {
                        mVideoPPS = buffer.mid(mVideoSPS.size(), i-mVideoSPS.size());
                    }
                    if(!mVideoSPS.isEmpty() && !mVideoPPS.isEmpty())
                        break;
                }
            }
        }
        mVideoSPS.remove(0, prefixSize);
        mVideoFrameCount++;
        type = ((uchar)mVideoSPS.at(0) & 31);
        if(VERBOSE)
            qDebug()<<"----VideoFrames"<<mVideoFrameCount<<0<<mVideoSPS.size()<<type;
        if(mRTMPPublisher!=NULL) {
            frame.buffer = mVideoSPS;
            frame.dts = 0;
            frame.pts = frame.dts;
            mTotalBytesDecoded += frame.buffer.size();
            mRTMPPublisher->postFrame(frame);
#if(FRAMESWRITER_ENABLED)
            if(mFramesWriter!=NULL)
                mFramesWriter->postFrame(frame);
#endif
        } else
            qDebug()<<"----RTMPPublisher is NULL! Video!";
        mVideoPPS.remove(0, prefixSize);
        mVideoFrameCount++;
        type = ((uchar)mVideoSPS.at(0) & 31);
        if(VERBOSE)
            qDebug()<<"----VideoFrames"<<mVideoFrameCount<<1<<mVideoPPS.size()<<type;
        if(mRTMPPublisher!=NULL) {
            frame.buffer = mVideoPPS;
            frame.dts = 1;
            frame.pts = frame.dts;
            mTotalBytesDecoded += frame.buffer.size();
            mRTMPPublisher->postFrame(frame);
#if(FRAMESWRITER_ENABLED)
            if(mFramesWriter!=NULL)
                mFramesWriter->postFrame(frame);
#endif
        } else
            qDebug()<<"----RTMPPublisher is NULL! Video!";
    }
    if(isKeyFrame) {
        prefixSize *= 3;
        prefixSize += mVideoSPS.size()+mVideoPPS.size();
    }
    buffer.remove(0, prefixSize);  //Remove NAL prefix
    long ts = (timestamp-this->mVideoStartTS);
    mRemainingVideoTS += ts%1000;
    ts /= 1000;
    if(mRemainingVideoTS>=1000) {
        ts += 1;
        mRemainingVideoTS -= 1000;
    }
    mVideoFrameCount++;
    type = ((uchar)buffer.at(0) & 31);
    if(VERBOSE)
        qDebug()<<"----VideoFrames"<<mVideoFrameCount<<isKeyFrame<<ts<<buffer.size()<<type;
    if(mRTMPPublisher!=NULL) {
        frame.buffer = buffer;
        frame.dts = ts+2;   //Adding 2 for SPS & PPS
        frame.pts = frame.dts;
        mTotalBytesDecoded += frame.buffer.size();
        mRTMPPublisher->postFrame(frame);
#if(FRAMESWRITER_ENABLED)
        if(mFramesWriter!=NULL)
            mFramesWriter->postFrame(frame);
#endif
    } else
        qDebug()<<"----RTMPPublisher is NULL! Video!";
    this->mLastVideoTS = ts;
}

QString Controller::serverDisplay()
{
    qDebug()<<mHost<<mPort<<mApp<<mPlayPath;
    if(mHost.isEmpty() || mApp .isEmpty() || mPlayPath.isEmpty()) return QString();
    return QString("%1:%2/%3/%4").arg(mHost).arg(mPort).
            arg(mApp).arg(mPlayPath);
}


bool Controller::setServer(QString serverUrl, bool doSave) {
    if(!serverUrl.startsWith("rtmp://") &&
            !serverUrl.startsWith("https://") &&
            !serverUrl.startsWith("http://"))
        serverUrl = "rtmp://"+serverUrl;
    QUrl url(serverUrl);
    if(!url.isValid()) return false;
    setHost(url.host());
    setPort(url.port(1935));
    QString path = url.path();
    if(path.startsWith("/"))
        path = path.right(path.size()-1);
    if(!path.contains("/") ||
            path.count("/")!=1)
        return false;
    setApp(path.split("/").first());
    setPlayPath(path.split("/").last());
    if(doSave) {
        QSettings settings("ShowStopper", "StreamCam");
        settings.setValue(KEY_SERVER_URL, serverUrl);
    }
    return true;
}

int Controller::droppedFramesCount() {
    if(mRTMPPublisher!=NULL)
        return mRTMPPublisher->droppedFramesCount();
    else
        return 0;
}

int Controller::totalFramesCount() {
    if(mRTMPPublisher!=NULL)
        return mRTMPPublisher->totalFramesCount();
    else
        return 0;
}

int Controller::audioFramesCount() {
    if(mRTMPPublisher!=NULL)
        return mRTMPPublisher->audioFramesCount();
    else
        return 0;
}
int Controller::videoFramesCount() {
    if(mRTMPPublisher!=NULL)
        return mRTMPPublisher->videoFramesCount();
    else
        return 0;
}

qint64 Controller::totalBytesSent() {
    if(mRTMPPublisher!=NULL)
        return mRTMPPublisher->totalBytesWritten();
    else
        return 0;
}



