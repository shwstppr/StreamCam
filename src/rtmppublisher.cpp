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

#include "rtmppublisher.h"
#include <QDateTime>

RTMPPublisher::RTMPPublisher(QString host,
            int port,
            QString app,
            QString playPath,
            QObject* parent)
    :QObject(parent),
     mHost(host), mPort(port), mApp(app), mPlayPath(playPath) {
    mLock = new QMutex();
    mAACHeader.clear();
    mHasAudio = false;
    mHasVideo = false;
    mAudioQueue.clear();
    mVideoQueue.clear();
}

RTMPPublisher::~RTMPPublisher() {
    if(isSocketConnected()) {
        qDebug()<<"Socket still connected! Need to close.";
        mSocket->flush();
        mSocket->close();
    }
}

void RTMPPublisher::run() {
    qDebug()<<"Thread";
    QTime iTime;
    mIsStopped = false;
    bool video = false;
    bool audio = false;
    iTime.start();
    handshake();
    qDebug()<<"handshake done!";
    setChunkSize();
    qDebug()<<"chunk size set!";
    connect();
    qDebug()<<"connect done!";
    publish();
    qDebug()<<"publish done!";
    readAll();
    qDebug()<<"Success!"<<iTime.elapsed();
    while (!mIsStopped && isSocketConnected()) {
        //qDebug()<<"I'm running!0";
        mLock->lock();
        MediaFrame audioFrame;
        MediaFrame videoFrame;
        MediaFrame frame;
        while (!mIsStopped && isSocketConnected()) {
            //qDebug()<<"I'm running!";
            if (!mIsStopped && this->mAudioQueue.isEmpty() && audio) {
                mCondition.wait(mLock);// this->condition.await();
            } else {
                if (!this->mAudioQueue.isEmpty()) {
                    audioFrame = this->mAudioQueue.first();
                    audio = true;
                    if (audioFrame.type == MediaFrame::EOS) {
                        return;
                    }
                }
                while (!mIsStopped && this->mVideoQueue.isEmpty() && video) {
                    mCondition.wait(mLock);// this->condition.await();
                }
                if (!this->mVideoQueue.isEmpty()) {
                    videoFrame = this->mVideoQueue.first();
                    video = true;
                    if (videoFrame.type == MediaFrame::EOS) {
                        return;
                    }
                }
//                iTime.restart();
                if (!audioFrame.isEmpty() && (videoFrame.isEmpty() || audioFrame.dts < videoFrame.dts)) {
                    if(VERBOSE)
                        qDebug()<<"--Sending AUDIO DTS"<<audioFrame.dts<<"size"<<audioFrame.buffer.size()<<"Video Queue Size"<<mVideoQueue.size();
                    frame = audioFrame;
                    mAudioQueue.removeFirst();
                } else if (!videoFrame.isEmpty()) {
                    if(VERBOSE)
                        qDebug()<<"----Sending VIDEO DTS"<<videoFrame.dts<<"size"<<videoFrame.buffer.size()<<"Audio Queue Size"<<mAudioQueue.size();
                    frame = videoFrame;
                    mVideoQueue.removeFirst();
                } else {
                    if(!mIsStopped)
                        mCondition.wait(mLock);// this->condition.await();
                }
                if (!frame.isEmpty()) {
                    mLock->unlock();    //this.lock.unlock();
                    readAll();
//                    qDebug()<<frame.type<<"Time taken"<<iTime.elapsed();
                    switch (frame.type) {
                        case MediaFrame::AUDIO:
                            sendAudioFrame(frame.buffer, frame.pts);
                            break;
                        case MediaFrame::VIDEO:
                            sendVideoNal(frame.buffer, frame.pts, frame.dts);
                            break;
                        default:
                            break;
                    }
                    break;
                }
            }

            //qDebug()<<"I'm running!10";
        }
    }
    qDebug()<<"yellow"<<mIsStopped;
    if(mIsStopped) {
        qDebug()<<"Destroying socket!";
        destroySocket();
        emit finished();
    }
}

bool RTMPPublisher::initiate() {
    mSocket = new QTcpSocket(this);
    mSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    QObject::connect(mSocket,SIGNAL(error(QAbstractSocket::SocketError)),
            this,SLOT(on_mSocket_error(QAbstractSocket::SocketError)));
    QObject::connect(mSocket,SIGNAL(bytesWritten(qint64)),
                this,SLOT(on_mSocket_bytesWritten(qint64)));
    mSocket->connectToHost(this->mHost, this->mPort);
    if(mSocket->waitForConnected(30*1000)) {
        qDebug()<<"Connection established!";
        return true;
    } else {
        qDebug()<<"Couldn't connect!";
        //Error signal here
        return false;
    }
}

void RTMPPublisher::start() {
    mAACFormat = 0;
    mHasAudio = false;
    mAudioTimestamp = 0;
    mHasVideo = false;
    mVideoTimestamp = 0;
    mNumChannels = 0;
    mSampleRate = 0;
    mSampleSize = 0;
    mIsWaitingForHandshakeReadyRead = false;
    mTotalBytesWritten = 0;
    mAudioFramesReceivedCount = 0;
    mVideoFramesReceivedCount = 0;
    mDroppedFramesCount = 0;
    mLastReceivedFrameTS = 0;
    if(initiate()) {
        run();
    }
}

void RTMPPublisher::handshake() {
    /* RTMP handshake requires following steps for handshake,
     * Client sends a message of 1537bytes first. First byte here is protocol version 0x03.
     * Next 4 bytes are epoch timestamp, next four all 0 and rest random digest.
     * Although whole 1536bytes can be random i practice, I'm not sure why!
     * Server responds by sending protocol version, 0x03 and then its own 1536bytes digest.
     * Client reads these bytes and responds by sending server's digest.
     * To this server responds by sending client's digest.
     * Effectively both client and server send three messages, ie.,
     * - protocol version
     * - own digest
     * - received digest
     */
    unsigned char buffer[3073];
    buffer[0] = (unsigned char) 3;
    for (int i = 1; i < 3073; i++) {
        buffer[i] = (unsigned char) 0;
    }
    if(isSocketConnected() &&
            mSocket->bytesAvailable()<0)
        mSocket->readAll(); //Clearing?
    write(buffer, 1537);
    int startTime = QDateTime::currentDateTime().toTime_t();
    mIsWaitingForHandshakeReadyRead = true;
    while(isSocketConnected() &&
            mSocket->bytesAvailable()<3073 &&
            QDateTime::currentDateTime().toTime_t()-startTime<2*60*1000) {
        qDebug()<<"Waiting for handshake read!";
        waitForReadyRead();
    }
    mIsWaitingForHandshakeReadyRead = false;
    if(isSocketConnected() &&
            mSocket->bytesAvailable()>=3073) {
        QByteArray buf = mSocket->read(3073);
        buf = buf.left(1536);
        write(buf);
    } else {
        qDebug()<<"Handshake error!"<<mSocket->bytesAvailable();
    }
}

void RTMPPublisher::setChunkSize() {
    /*
	 * First byte, Chunk Basic Header, here is 4. First two bits represent fmt, ie, Type 0 chunk header type (header will be 11 bytes).
	 * - Next six bits represent chunk stream id, ie, 4 for a User control message.?
     * For 11 byte header,
	 * - First 3 are for timestamp, ignored here.
	 * - Next 3 are payload length which is 4 for this.
     * - Next 1 byte is for protocol message type which is 1 here.
     * - Next 4 bytes are for message stream id which will be 0 in this case.
     * Remaining 4 for are payload which is chunk size here with first bit as 0.
     */
    int bufferLength = 16;
    unsigned char buffer[16] = {(unsigned char) 4, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 4, (unsigned char) 1, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0};
    buffer[bufferLength - 4] = (unsigned char) (CHUNK_SIZE >> 24);
    buffer[bufferLength - 3] = (unsigned char) (CHUNK_SIZE >> 16);
    buffer[bufferLength - 2] = (unsigned char) (CHUNK_SIZE >> 8);
    buffer[bufferLength - 1] = (unsigned char) CHUNK_SIZE;
    write(buffer, bufferLength);
}

void RTMPPublisher::connect() {
    /*
	 * First byte, Chunk Basic Header, here is 4. First two bits represent fmt, ie, Type 0 chunk header type (header will be 11 bytes).
	 * - Next six bits represent chunk stream id, ie, 4 for a User control message.?
     * For 11 byte header,
	 * - First 3 are for timestamp, ignored here.
	 * - Next 3 are payload length which will be mApp.length()+31 here.
     * - Next 1 byte is for protocol message type which is 20 here as it is a Command Message.
     * - Next 4 bytes are for message stream id which will be 0 in this case.
     * Remaining bytes 28(in buffer)+3(in bufferEnd)+mApp.length() are for payload which is AMF encoded connect message.
     */
    int bufferLength = 40;
    unsigned char buffer[40] = {(unsigned char) 4, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 20, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 2, (unsigned char) 0, (unsigned char) 7, (unsigned char) 99, (unsigned char) 111, (unsigned char) 110, (unsigned char) 110, (unsigned char) 101, (unsigned char) 99, (unsigned char) 116, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 3, (unsigned char) 0, (unsigned char) 3, (unsigned char) 97, (unsigned char) 112, (unsigned char) 112, (unsigned char) 2, (unsigned char) 0, (unsigned char) 0};
    unsigned char bufferEnd[3] = {(unsigned char) 0, (unsigned char) 0, (unsigned char) 9};
    int length = mApp.length() + 31;
    buffer[4] = (unsigned char) (length >> 16);
    buffer[5] = (unsigned char) (length >> 8);
    buffer[6] = (unsigned char) length;
    buffer[bufferLength - 2] = (unsigned char) (mApp.length() >> 8);
    buffer[bufferLength - 1] = (unsigned char) mApp.length();
    qDebug()<<"Pinky"<<QString::fromUtf8(reinterpret_cast<char*>(buffer), 40);
    write(buffer, bufferLength);//    this.out.write(buffer);
    write(this->mApp.toUtf8());//    this.out.write(this.app.getBytes());
    write(bufferEnd, 3);//    this.out.write(bufferEnd);
}

void RTMPPublisher::publish() {
    /*
	 * First byte, Chunk Basic Header, here is 4. First two bits represent fmt, ie, Type 0 chunk header type (header will be 11 bytes).
	 * - Next six bits of first byte represent chunk stream id, ie, 4 for a User control message.?
     * For 11 byte header,
	 * - First 3 are for timestamp, ignored here.
	 * - Next 3 are payload length which will be mPlayPath.length()+23 here.
     * - Next 1 byte is for protocol message type which is 20 here as it is a Command Message.
     * - Next 4 bytes are for message stream id which will be 0 in this case.
     * Remaining bytes 23(in buffer)+mPlayPath.length() are for payload which is AMF encoded connect message.
     */
    int bufferLength = 35;
    unsigned char buffer[35] = {(unsigned char) 4, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 20, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 2, (unsigned char) 0, (unsigned char) 7, (unsigned char) 112, (unsigned char) 117, (unsigned char) 98, (unsigned char) 108, (unsigned char) 105, (unsigned char) 115, (unsigned char) 104, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 5, (unsigned char) 2, (unsigned char) 0, (unsigned char) 0};
    int length = mPlayPath.length() + 23;
    buffer[4] = (unsigned char) (length >> 16);
    buffer[5] = (unsigned char) (length >> 8);
    buffer[6] = (unsigned char) length;
    buffer[bufferLength - 2] = (unsigned char) (mPlayPath.length() >> 8);
    buffer[bufferLength - 1] = (unsigned char) mPlayPath.length();
    write(buffer, bufferLength);
    write(this->mPlayPath.toUtf8());
}

void RTMPPublisher::startAudio(long ts) {
    /*
	 * First byte, Chunk Basic Header, here is 8. First two bits represent fmt, ie, Type 0 chunk header type (header will be 11 bytes).
	 * - Next six bits of first byte represent chunk stream id, ie, 8 for a Audio.?
     * For 11 byte header,
	 * - First 3 are for timestamp, setting audio start timestamp here. Start timestamp will be 0 mostly.
	 * - Next 3 are payload length which will be mAACHeader.length()+2 here.
     * - Next 1 byte is for protocol message type which is 8 here as it is a Audio Message.
     * - Next 4 bytes are for message stream id which will be 0 in this case.
     * Remaining bytes 2(in buffer)+mAACHeader.length() are for payload, sending audio header.
     */
    unsigned char buffer[14] = {(unsigned char) 8, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 8, (unsigned char) 1, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0};
    qDebug()<<"Starting audio";
    this->mAudioTimestamp = ts;
    buffer[1] = (unsigned char) ((int) ((ts >> 16) & 255));
    buffer[2] = (unsigned char) ((int) ((ts >> 8) & 255));
    buffer[3] = (unsigned char) ((int) (255 & ts));
    int length = this->mAACHeader.length() + 2;
    buffer[4] = (unsigned char) ((length >> 16) & 255);
    buffer[5] = (unsigned char) ((length >> 8) & 255);
    buffer[6] = (unsigned char) (length & 255);
    this->mAACFormat = (((this->mNumChannels - 1) & 1) | 172) | (((this->mSampleSize - 1) & 1) << 1);
    buffer[12] = (unsigned char) this->mAACFormat;
    write(buffer, 14);
    write(this->mAACHeader);
    this->mHasAudio = true;
}

void RTMPPublisher::sendAudioFrame(QByteArray frame, long ts) {
    /*
	 * First byte, Chunk Basic Header, here is 72. First two bits represent fmt, ie, Type 1 chunk header type (header will be 7 bytes with no message stream id field).
	 * - Next six bits of first byte represent chunk stream id, ie, 8 for a Audio.?
     * For 7 byte header,
	 * - First 3 are for timestamp delta relative to timestamp sent in previous chunk.
	 * - Next 3 are payload length which will be frame.length()+2 here.
     * - Next 1 byte is for protocol message type which is 8 here as it is a Audio Message.
     * Remaining bytes 2(in buffer)+frame.length() are for payload, sending audio.
	 * If payload size is greater than CHUNK_SIZE then it is split. Byte value 200 is used for splitting.
     */
    if (!((this->mAACHeader.isNull() || this->mAACHeader.isEmpty()) || this->mHasAudio)) {
        startAudio(ts);
    }
    if (this->mHasAudio) {
        unsigned char buffer[10] = {(unsigned char) 72, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 8, (unsigned char) 0, (unsigned char) 1};
        long delta = ts - this->mAudioTimestamp;
        this->mAudioTimestamp = ts;
        buffer[1] = (unsigned char) ((int) ((delta >> 16) & 255));
        buffer[2] = (unsigned char) ((int) ((delta >> 8) & 255));
        buffer[3] = (unsigned char) ((int) (255 & delta));
        int totalLength = frame.length() + 2;
        buffer[4] = (unsigned char) ((totalLength >> 16) & 255);
        buffer[5] = (unsigned char) ((totalLength >> 8) & 255);
        buffer[6] = (unsigned char) (totalLength & 255);
        buffer[8] = (unsigned char) this->mAACFormat;
        QByteArray buf(reinterpret_cast<char*>(buffer), 10);
        int length = CHUNK_SIZE - 2;
        if (length > frame.length()) {
            length = frame.length();
        }
        buf.append(frame.left(length));
        write(buf, false);
        int offset = length;
        while (offset < frame.length()) {
            length = CHUNK_SIZE;
            if (offset + length > frame.length()) {
                length = frame.length() - offset;
            }
            write(IntToArray(200));
            write(frame.mid(offset, length), false);
            offset += length;
        }
    } else {
        qDebug()<<"Skip audio frame";
    }
}

void RTMPPublisher::startVideo(long ts) {
    /*
	 * First byte, Chunk Basic Header, here is 9. First two bits represent fmt, ie, Type 0 chunk header type (header will be 11 bytes).
	 * - Next six bits of first byte represent chunk stream id, ie, 9 for a Video.?
     * For 11 byte header,
	 * - First 3 are for timestamp, setting video start timestamp here. Start timestamp will be 0 mostly.
	 * - Next 3 are payload length which will be mSPS.length() + 16) + mPPS.length() here.
     * - Next 1 byte is for protocol message type which is 9 here as it is a Video Message.
     * - Next 4 bytes are for message stream id which will be 0 in this case.
     * Remaining bytes 13(in buffer)+3(in bufferEnd)+mSPS.length() + mPPS.length() are for payload, sending video sps, pps.
     */
    int bufferLength = 25;
    unsigned char buffer[25] = {(unsigned char) 9, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 9, (unsigned char) 1, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 23, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 1, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) -1, (unsigned char) -31, (unsigned char) 0, (unsigned char) 0};
    unsigned char bufferEnd[3] = {(unsigned char) 1, (unsigned char) 0, (unsigned char) 0};
    qDebug()<<"Starting video";
    this->mVideoTimestamp = ts;
    buffer[1] = (unsigned char) ((int) ((ts >> 16) & 255));
    buffer[2] = (unsigned char) ((int) ((ts >> 8) & 255));
    buffer[3] = (unsigned char) ((int) (255 & ts));
    int length = (this->mSPS.length() + 16) + this->mPPS.length();
    buffer[4] = (unsigned char) ((length >> 16) & 255);
    buffer[5] = (unsigned char) ((length >> 8) & 255);
    buffer[6] = (unsigned char) (length & 255);
    buffer[bufferLength - 2] = (unsigned char) ((this->mSPS.length()>> 8) & 255);
    buffer[bufferLength - 1] = (unsigned char) (this->mSPS.length() & 255);
    bufferEnd[1] = (unsigned char) ((this->mPPS.length() >> 8) & 255);
    bufferEnd[2] = (unsigned char) (this->mPPS.length() & 255);
    write(buffer, bufferLength);
    write(this->mSPS);
    write(bufferEnd, 3);
    write(this->mPPS);
    this->mHasVideo = true;
}

void RTMPPublisher::sendVideoNal(QByteArray nal, long pts, long dts) {
    /*
	 * First byte, Chunk Basic Header, here is 73. First two bits represent fmt, ie, Type 1 chunk header type (header will be 7 bytes with no message stream id field).
	 * - Next six bits of first byte represent chunk stream id, ie, 9 for a Video.?
     * For 7 byte header,
	 * - First 3 are for timestamp delta relative to timestamp sent in previous chunk.
	 * - Next 3 are payload length which will be frame.length()+2 here.
     * - Next 1 byte is for protocol message type which is 9 here as it is a Video Message.
     * Remaining bytes 9(in buffer)+nal.length() are for payload, sending video. DTS, PTS delay has also been taken care of.
	 * If payload size is greater than CHUNK_SIZE then it is split. Byte value 201 is used for splitting.
     */
    int nalType = nal[0] & 31;
    if (nalType == 7) {
        qDebug()<<"SPS arrived";
        this->mSPS = nal;
    } else if (nalType == 8) {
        qDebug()<<"PPS arrived";
        this->mPPS = nal;
    } else {
        if (!(this->mSPS.isNull() || this->mSPS.isEmpty() ||
                this->mPPS.isNull() || this->mPPS.isEmpty() ||
                this->mHasVideo)) {
            startVideo(dts);
        }
        if (this->mHasVideo) {
            int bufferLength = 17;
            unsigned char buffer[17] = {(unsigned char) 73, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 9, (unsigned char) 39, (unsigned char) 1, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0, (unsigned char) 0};
            long delta = dts - this->mVideoTimestamp;
            this->mVideoTimestamp = dts;
            buffer[1] = (unsigned char) ((int) (255 & (delta >> 16)));
            buffer[2] = (unsigned char) ((int) (255 & (delta >> 8)));
            buffer[3] = (unsigned char) ((int) (255 & delta));
            int totalLength = nal.length() + 9;
            buffer[4] = (unsigned char) ((totalLength >> 16) & 255);
            buffer[5] = (unsigned char) ((totalLength >> 8) & 255);
            buffer[6] = (unsigned char) (totalLength & 255);
            if (nalType == 5) {
                buffer[8] = (unsigned char) 23;
            }
            long delay = pts - dts;
            buffer[bufferLength - 7] = (unsigned char) ((int) (255 & (delay >> 16)));
            buffer[bufferLength - 6] = (unsigned char) ((int) (255 & (delay >> 8)));
            buffer[bufferLength - 5] = (unsigned char) ((int) (255 & delay));
            buffer[bufferLength - 4] = (unsigned char) ((nal.length() >> 24) & 255);
            buffer[bufferLength - 3] = (unsigned char) ((nal.length() >> 16) & 255);
            buffer[bufferLength - 2] = (unsigned char) ((nal.length() >> 8) & 255);
            buffer[bufferLength - 1] = (unsigned char) (nal.length() & 255);
            QByteArray buf(reinterpret_cast<char*>(buffer), bufferLength);
            int length = CHUNK_SIZE - 9;
            if (length > nal.length()) {
                length = nal.length();
            }
            buf.append(nal.left(length));
            write(buf, false);
            int offset = length;
            while (offset < nal.length()) {
                length = CHUNK_SIZE;
                if (offset + length > nal.length()) {
                    length = nal.length() - offset;
                }
                write((uchar)201);
                write(nal.mid(offset, length), false);
                offset += length;
            }
        } else {
            qDebug()<<"Skip video frame";
        }
    }
}

void RTMPPublisher::setAudioHeader(QByteArray header, int nchan, int srate, int ssize) {
    this->mAACHeader = header;
    this->mNumChannels = nchan;
    this->mSampleRate = srate;
    this->mSampleSize = ssize;
}

bool RTMPPublisher::isSocketConnected() {
    if(mSocket &&
            mSocket->state() == QAbstractSocket::ConnectedState)
        return true;
    return false;
}

bool RTMPPublisher::waitForReadyRead(int msec) {
    if(isSocketConnected())
        return mSocket->waitForReadyRead(msec);
    return false;
}

QByteArray RTMPPublisher::readAll() {
    if(isSocketConnected())
        return mSocket->readAll();
    return QByteArray();
}

qint64 RTMPPublisher::write(const QByteArray data, const bool doWait)
{
    if(isSocketConnected()) {
#if(LOG_HIGH_WRITE_TIMES)
        logTimer.restart();
#endif
        qint64 written = mSocket->write(data); //write the data itself
        if(doWait)
            mSocket->waitForBytesWritten();
        else
            mSocket->flush();
#if(LOG_HIGH_WRITE_TIMES)
        if(logTimer.elapsed()>HIGH_WRITE_LIMIT_MSEC)
            qDebug()<<data.size()<<"QByteArray"<<logTimer.elapsed()<<written;
#endif
        return written;
    }
    return 0;
}

qint64 RTMPPublisher::write(const char* data, qint64 length, const bool doWait)
{
    if(isSocketConnected()) {
#if(LOG_HIGH_WRITE_TIMES)
        logTimer.restart();
#endif
        qint64 written = mSocket->write(data, length); //write the data itself
        if(doWait)
            mSocket->waitForBytesWritten();
        else
            mSocket->flush();
#if(LOG_HIGH_WRITE_TIMES)
        if(logTimer.elapsed()>HIGH_WRITE_LIMIT_MSEC)
            qDebug()<<length<<"char"<<logTimer.elapsed()<<written;
#endif
        return written;
    }
    return 0;
}

qint64 RTMPPublisher::write(const unsigned char* data, qint64 length, const bool doWait)
{
    return write(reinterpret_cast<const char*>(data), length, doWait);
}

qint64 RTMPPublisher::write(const unsigned char data, const bool doWait)
{
    return write(reinterpret_cast<const char*>(&data), 1, doWait);
}

qint64 RTMPPublisher::write(qint32 data, const bool doWait)
{
    return write(IntToArray(data), doWait);
}

void RTMPPublisher::on_mSocket_readyRead() {

}

void RTMPPublisher::postFrame(MediaFrame frame)
{
    if(this->mIsStopped) return;
    mLock->lock();
    if (frame.type == MediaFrame::EOS) {
        this->mVideoQueue.append(frame);
        this->mAudioQueue.append(frame);
        mCondition.wakeAll();
    } else {
        mLastReceivedFrameTS = frame.dts;
        QLinkedList<MediaFrame> *queue;
        if(frame.type == MediaFrame::AUDIO) {
            queue = &mAudioQueue;
            mAudioFramesReceivedCount++;
            emit audioFramesCountChanged();
        } else {
            queue = &mVideoQueue;
            mVideoFramesReceivedCount++;
            emit videoFramesCountChanged();
        }
        if ((frame.type == MediaFrame::AUDIO && queue->size() >= 2*MAX_QUEUE_SIZE) ||
                (frame.type == MediaFrame::VIDEO && queue->size() >= MAX_QUEUE_SIZE)) {
//            qDebug()<<"Drop frame";
            mDroppedFramesCount++;
            emit droppedFramesCountChanged();
        } else {
            queue->append(frame);
            mCondition.wakeAll();
        }
    }
    mLock->unlock();
}

void RTMPPublisher::safeStop() {
    qDebug()<<"RTMPPublisher"
            <<(mAudioFramesReceivedCount+mVideoFramesReceivedCount)
            <<"frames received in"
            <<mLastReceivedFrameTS/1000<<"secs";
    qDebug()<<"RTMPPublisher"
            <<mDroppedFramesCount
            <<"frames dropped out of"
            <<(mAudioFramesReceivedCount+mVideoFramesReceivedCount);
    qDebug()<<"RTMPPublisher"
            <<mTotalBytesWritten/1024
            <<"kb data written";
    mIsStopped = true;
    mCondition.wakeAll();
    qDebug()<<"Condition awaked!";
    //Not closing/destroying socket here. It will be closed by class itself
//    destroySocket();
//    qDebug()<<"Socket destroyed!";
//    emit finished();
}

void RTMPPublisher::destroySocket() {
    if(mSocket) {
        if(mSocket->state()==QAbstractSocket::ConnectedState) {
            mSocket->flush();
            mSocket->close();
            qDebug()<<"Socket closed!";
        } else {
            mSocket->reset();
            qDebug()<<"Socket reset!";
        }
        mSocket->deleteLater();
        mSocket = NULL;
    }
}

void RTMPPublisher::on_mSocket_error(QAbstractSocket::SocketError error)
{
    if(error==QAbstractSocket::SocketTimeoutError && mIsWaitingForHandshakeReadyRead)
        return;
    qDebug()<<"RTMPPublisher-socketError"<<error;
    destroySocket();
    emit socketError(error);
}

void RTMPPublisher::on_mSocket_bytesWritten(qint64 bytes)
{
    mTotalBytesWritten += bytes;
//    qDebug()<<"Total"<<mTotalBytesWritten/1024;
}

QByteArray RTMPPublisher::IntToArray(qint32 source) //Use qint32 to ensure that the number have 4 bytes
{
    //Avoid use of cast, this is the Qt way to serialize objects
    QByteArray temp;
    QDataStream data(&temp, QIODevice::ReadWrite);
    data << source;
    return temp;
}
