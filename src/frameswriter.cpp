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

#include "frameswriter.h"
#include <QDateTime>
#include <QDebug>

FramesWriter::FramesWriter(QString path, QObject* parent)
    :QObject(parent),
     mWriteLocation(path),
     mIsStopped(false),
     mAudioFramesReceivedCount(0),
     mVideoFramesReceivedCount(0),
     mDroppedFramesCount(0)
{
    if(path.isEmpty())
        path = "shared/documents/"+QDateTime::currentDateTime().toString("dd-MMM-yy hh:mm:ssAP");
    mWriteLocation = path;
    QDir dir;
    dir.mkpath(mWriteLocation);
    mLock = new QMutex();
}

FramesWriter::~FramesWriter() {}

void FramesWriter::start()
{
    mAudioQueue.clear();
    mVideoQueue.clear();
    mIsStopped = false;
    run();
}

void FramesWriter::postFrame(MediaFrame frame)
{
    if(this->mIsStopped) return;
    mLock->lock();
    if (frame.type == MediaFrame::EOS) {
        this->mVideoQueue.append(frame);
        this->mAudioQueue.append(frame);
        mCondition.wakeAll();
    } else {
        QLinkedList<MediaFrame> *queue;
        if(frame.type == MediaFrame::AUDIO) {
            queue = &mAudioQueue;
            mAudioFramesReceivedCount++;
        } else {
            queue = &mVideoQueue;
            mVideoFramesReceivedCount++;
        }
        if (queue->size() >= MAX_QUEUE_SIZE) {
//            qDebug()<<"Drop frame";
            mDroppedFramesCount++;
        } else {
            queue->append(frame);
            mCondition.wakeAll();
        }
    }
    mLock->unlock();
}

void FramesWriter::run()
{
    qDebug()<<"Thread";
    mIsStopped = false;
    bool video = false;
    bool audio = false;
    while (!mIsStopped) {
        mLock->lock();
        MediaFrame audioFrame;
        MediaFrame videoFrame;
        MediaFrame frame;
        while (!mIsStopped) {
            if (!mIsStopped && this->mAudioQueue.isEmpty() && audio) {
                mCondition.wait(mLock);
            } else {
                if (!this->mAudioQueue.isEmpty()) {
                    audioFrame = this->mAudioQueue.first();
                    audio = true;
                    if (audioFrame.type == MediaFrame::EOS) {
                        return;
                    }
                }
                while (!mIsStopped && this->mVideoQueue.isEmpty() && video) {
                    mCondition.wait(mLock);
                }
                if (!this->mVideoQueue.isEmpty()) {
                    videoFrame = this->mVideoQueue.first();
                    video = true;
                    if (videoFrame.type == MediaFrame::EOS) {
                        return;
                    }
                }
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
                        mCondition.wait(mLock);
                }
                if (!frame.isEmpty()) {
                    mLock->unlock();
                    switch (frame.type) {
                        case MediaFrame::AUDIO:
                            writeAudioFrame(1000000+frame.pts, frame.buffer);
                            break;
                        case MediaFrame::VIDEO:
                            writeVideoFrame(1000000+frame.pts, frame.buffer);
                            break;
                        default:
                            break;
                    }
                    break;
                }
            }
        }
    }
    qDebug()<<"Thread finished"<<mIsStopped;
    if(mIsStopped)
        emit finished();
}

bool FramesWriter::writeAudioFrame(const long timestamp, const QByteArray data)
{
    QFile file(mWriteLocation+"/"+QString::number(timestamp)+"-AUDIO.frame");
    if(file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
        return true;
    }
    return false;
}

bool FramesWriter::writeVideoFrame(const long timestamp, const QByteArray data)
{
    QFile file(mWriteLocation+"/"+QString::number(timestamp)+"-VIDEO.frame");
    if(file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
        return true;
    }
    return false;
}

void FramesWriter::safeStop()
{
    mIsStopped = true;
    mCondition.wakeAll();
}
