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

#include "StreamCam.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/LocaleHandler>
#include <bb/cascades/Window>
#include <bb/cascades/ForeignWindowControl>
#include <bb/cascades/OrientationSupport>
#include <bb/cascades/LayoutUpdateHandler>
#include <bps/soundplayer.h>
#include <fcntl.h>
#include <camera/camera_encoder.h>
#include <QtSensors/QOrientationSensor>
#include <QSettings>

using namespace bb::cascades;
using namespace QtMobility;

// when trying to open the camera, retry for this amount of time in order to handle
// corner cases where another app may still have the camera open
#define RETRY_MS    1000
#define RETRY_DELAY (RETRY_MS/10)

// qDebug() now logs to slogger2, which I find inconvenient since the NDK does not pick this up in the console,
// so I am installing a custom handler to log to stderr.
static void log_to_stderr(QtMsgType msgType, const char *msg)
{
    (void)msgType;  // go away, warning!
    fprintf(stderr, "%s\n", msg);
}


StreamCam::StreamCam(bb::cascades::Application *app) :
        QObject(app),
        mUnit(CAMERA_UNIT_NONE),
        mHandle(CAMERA_HANDLE_INVALID),
        mState(StateIdle),
        mDeferredResourceWarning(false),
        mStatusThread(NULL),
        mApp(app),
        mRequireUprightVf(false),
        mRequireUprightCapture(false),
        mSwitchingCamera(false),
        mOrientationSensor(NULL),
        mVideoFileDescriptor(-1),
        mVideoRotations(NULL),
        mVideoVfRotations(NULL),
        mVideoResolutions(NULL),
        mVideoVfResolutions(NULL),
        mSwitchCameraTimer(NULL),
        mCameraResolutionsModel(NULL),
        mVideoBitrate(360),
        mVideoFramerate(30.0)
{
    // NOTE: since we are passed the Application instance when constructed, we can just cache it for later.
    // if this code is eventually migrated into a custom control, then we would instead use Application::instance()

    // TODO: this is just for my own debugging purposes. should not ship with this.
    //qInstallMsgHandler(log_to_stderr);

    qmlRegisterType<QTimer>("com.streamcam.timer", 1, 0, "Timer");
    qmlRegisterUncreatableType<StreamCam>("com.streamcam.cam", 1, 0, "StreamCam", "StreamCam is uncreatable");
    qmlRegisterUncreatableType<Controller>("com.streamcam.streamcontroller", 1, 0, "StreamController", "StreamController is uncreatable");
    qRegisterMetaType<camera_devstatus_t>("camera_devstatus_t");
    qRegisterMetaType<uint16_t>("uint16_t");

    //Read settings
    QSettings settings("ShowStopper", "StreamCam");
    if(!settings.value(KEY_VIDEO_FRAMERATE).toString().isEmpty())
        mVideoFramerate = settings.value(KEY_VIDEO_FRAMERATE).toDouble();
    if(!settings.value(KEY_VIDEO_BITRATE).toString().isEmpty())
        mVideoBitrate = settings.value(KEY_VIDEO_BITRATE).toDouble();
    if(!settings.value(KEY_VIDEO_RESOLUTION).toString().isEmpty())
        setPreferredCameraSize(settings.value(KEY_VIDEO_RESOLUTION).toString());

    mController = new Controller(this);
    if(!settings.value(KEY_SERVER_URL).toString().isEmpty())
        mController->setServer(settings.value(KEY_SERVER_URL).toString(), false);
    if (!QObject::connect(this,SIGNAL(streamingStart()),mController,SLOT(startStreaming()))) {
        qWarning() << "failed to connect streamingStart signal";
    }
    if (!QObject::connect(this,SIGNAL(streamingStop()),mController,SLOT(stopStreaming()))) {
        qWarning() << "failed to connect streamingStop signal";
    }
    if (!QObject::connect(mController,SIGNAL(publishError(QString)),this,SLOT(on_mController_publishError(QString)))) {
        qWarning() << "failed to connect publishError signal";
    }
    // prepare the localization
    m_pTranslator = new QTranslator(this);
    m_pLocaleHandler = new LocaleHandler(this);
    if(!QObject::connect(m_pLocaleHandler, SIGNAL(systemLanguageChanged()), this, SLOT(onSystemLanguageChanged()))) {
        // This is an abnormal situation! Something went wrong!
        // Add own code to recover here
        qWarning() << "Recovering from a failed connect()";
    }

    if (!QObject::connect(mApp, SIGNAL(thumbnail()), this, SLOT(onThumbnail()))) {
        qWarning() << "failed to connect thumbnail signal";
    }

    if (!QObject::connect(mApp, SIGNAL(fullscreen()), this, SLOT(onFullscreen()))) {
        qWarning() << "failed to connect fullscreen signal";
    }

    if (!QObject::connect(mApp, SIGNAL(invisible()), this, SLOT(onInvisible()))) {
        qWarning() << "failed to connect invisible signal";
    }

    if (!QObject::connect(OrientationSupport::instance(),
                          // previously, I was using orientationChanged(), but things may look better if we
                          // update the window before the rotation effect happens
                          SIGNAL(displayDirectionAboutToChange(bb::cascades::DisplayDirection::Type, \
                                                               bb::cascades::UIOrientation::Type)),
                          this,
                          SLOT(onDisplayDirectionChanging(bb::cascades::DisplayDirection::Type, \
                                                          bb::cascades::UIOrientation::Type)))) {
        qWarning() << "failed to connect displayDirectionAboutToChange signal";
    }
    // connect to the QOrientationSensor and get ready to receive readings
    if (startOrientationReadings() != EOK) {
        qWarning() << "failed to connect to orientation sensor?";
    }
    // indicate all orientations are supported
    OrientationSupport::instance()->setSupportedDisplayOrientation(SupportedDisplayOrientation::All);
    // initialize angles.  for the 2nd reading, we should probably be querying our QOrientationSensor, but I'm not sure if it will be
    // available yet, since we just started the sensor.  it's okay to pick another default for now.. we can update when the reading becomes
    // available.
    mDisplayDirection = OrientationSupport::instance()->displayDirection();
    mOrientationDirection = mDisplayDirection;
    updateAngles();

    // initial load
    onSystemLanguageChanged();

    // Create scene document from main.qml asset, the parent is set
    // to ensure the document gets destroyed properly at shut down.
    QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(this);

    qml->setContextProperty("Cam", this);
    qml->setContextProperty("streamController", this->mController);
    // Create root object for the UI
    mRoot = qml->createRootObject<AbstractPane>();

    // Set created root object as the application scene
    app->setScene(mRoot);

    // find the ForeignWindowControl and cache it for later
    mFwc = mRoot->findChild<ForeignWindowControl*>("vfForeignWindow");

    // hook into the layout engine to find the size of the parent container of the viewfinder FWC
    LayoutUpdateHandler::create((bb::cascades::Control*)(mFwc->parent()))
        .onLayoutFrameChanged(this, SLOT(onVfParentLayoutFrameChanged(QRectF)));

    // start up in video mode
    inventoryCameras();
    emit canCaptureChanged(mCanCapture = false);
    if(!mSwitchingCamera)
        emit capturingChanged(mCapturing = false);
    if (mHasRearCamera) {
        mUnit = CAMERA_UNIT_REAR;
    } else if (mHasFrontCamera) {
        mUnit = CAMERA_UNIT_FRONT;
    } else {
        mUnit = CAMERA_UNIT_NONE;
    }

    mResumeVfMode = ModeVideo;
    setCameraUnit((CameraUnit)mUnit);
    setVfMode(ModeVideo);
}


void StreamCam::onSystemLanguageChanged()
{
    QCoreApplication::instance()->removeTranslator(m_pTranslator);
    // Initiate, load and install the application translation files.
    QString locale_string = QLocale().name();
    QString file_name = QString("StreamCam_%1").arg(locale_string);
    if (m_pTranslator->load(file_name, "app/native/qm")) {
        QCoreApplication::instance()->installTranslator(m_pTranslator);
    }
}

int StreamCam::capture()
{
    int err = EOK;
    switch (mState) {
    case StateVideoVf:
        mStopViewfinder = false;
        err = runStateMachine(StateVideoCapture);
        break;
    case StateVideoCapture:
        mStopViewfinder = false;
        err = runStateMachine(StateVideoVf);
        break;
    default:
        qDebug() << "error, cannot capture in state" << stateName(mState);
        err = EINVAL;
        break;
    }
    return err;
}

int StreamCam::switchCaptureCamera()
{
    int err = EOK;
    if(mState==StateVideoCapture && mCapturing &&
            mHasFrontCamera && mHasRearCamera) {
        mSwitchingCamera = true;
        mStopViewfinder = false;
        err = runStateMachine(StateVideoVf);
        if(err!=EOK) {
            mSwitchingCamera = false;
            qDebug()<<"Problem switching camera!"<<"Unable to change to VideoVf mode"<<err;
            return err;
        }
        if((CameraUnit)mUnit == UnitRear)
            setCameraUnit(UnitFront);
        else
            setCameraUnit(UnitRear);
        if(mSwitchCameraTimer == NULL) {
            mSwitchCameraTimer = new QTimer(this);
            mSwitchCameraTimer->setInterval(512);
            mSwitchCameraTimer->setSingleShot(true);
            QObject::connect(mSwitchCameraTimer,SIGNAL(timeout()),this,SLOT(onSwitchCameraTimerTimeout()));
        }
        mSwitchCameraTimer->start();
//        mStopViewfinder = false;
//        err = runStateMachine(StateVideoCapture);
//        if(err!=EOK) {
//            mSwitchingCamera = false;
//            qDebug()<<"Problem switching camera!"<<"Unable to change to VideoCapture mode"<<err;
//            return err;
//        }
//        mSwitchingCamera = false;
    }
    return err;
}

void StreamCam::onSwitchCameraTimerTimeout() {
    if(mSwitchingCamera && mState==StateVideoVf) {
        mStopViewfinder = false;
        runStateMachine(StateVideoCapture);
    }
    mSwitchingCamera = false;
}

void StreamCam::debugVfSize()
{
    qDebug()<<mFwc->preferredWidth()<<mFwc->preferredHeight();
}

int StreamCam::openCamera(camera_unit_t unit)
{
    if (mHandle != CAMERA_HANDLE_INVALID) {
        qDebug() << "already have a camera open";
        return CAMERA_EALREADY;
    }
    prepareCameraResolutionsModel(unit);
    //Taking width, height opposite as camera in BB10 gives camera supported resolutions like this
    if(mPreferredCameraSize.isEmpty()) {
        mPreferredCameraSize = QSize(360, 640);
        emit outputWidthChanged();
        emit outputHeightChanged();
        qDebug()<<"Preferred camera video/preview size"<<mPreferredCameraSize;
    }
    // Implement a retry loop here to handle cases where another app may not have released the camera yet.
    // (eg. when switching between camera apps)
    int err;
    for (int retry=RETRY_MS; retry; retry-=RETRY_DELAY) {
        err = camera_open((camera_unit_t)unit, CAMERA_MODE_RW | CAMERA_MODE_ROLL, &mHandle);
        if (err == EOK) break;
        qDebug() << "failed to open camera unit" << unit << ": error" << err << "(will retry)";
        usleep(RETRY_DELAY * 1000);
    }
    if (err) {
        qDebug() << "could not open camera unit" << unit << ": error" << err;
        mHandle = CAMERA_HANDLE_INVALID;
        mUnit = CAMERA_UNIT_NONE;
    } else {
        qDebug() << "opened camera unit" << unit;
        mUnit = (camera_unit_t)unit;
        err = discoverCameraCapabilities();
        if (err) {
            qDebug() << "failed to query camera capabilities.";
            // just abort here
            closeCamera();
            return err;
        }
        // attach status event monitoring thread.
        // ordinarilly, we would just use a status callback via one of the viewfinder start functions,
        // but that would mean we would only be able to monitor status while a viewfinder is running.
        // registering explicitly for a status event this way allows us to monitor for camera powerdown/powerup events
        // as long as we still have the camera device open.
        mStatusThread = new StatusThread(this);
        if (!mStatusThread) {
            err = errno;
            qDebug() << "failed to attach status thread";
        } else {
            if (!QObject::connect(mStatusThread, SIGNAL(statusChanged(camera_devstatus_t, uint16_t)),
                                  this, SLOT(onStatusChanged(camera_devstatus_t, uint16_t)))) {
                qDebug() << "failed to connect statusChanged";
            } else {
                mStatusThread->start();
            }
        }
    }
    return err;
}


int StreamCam::closeCamera()
{
    if (mHandle == CAMERA_HANDLE_INVALID) {
        qDebug() << "no camera to close. ignoring.";
    } else {
        // detach the status thread
        if (mStatusThread) {
            mStatusThread->cleanShutdown();
            mStatusThread->wait();
            delete mStatusThread;
            mStatusThread = NULL;
        }
        qDebug() << "closing camera";
        camera_close(mHandle);
        mHandle = CAMERA_HANDLE_INVALID;
    }
    return EOK;
}


int StreamCam::setCameraUnit(CameraUnit unit)
{
    int err = EOK;
    // only allow this if we are not in a transitory state
    if (mState == StateStartingVideoVf) {
        return EINVAL;
    }
    err = runStateMachine(StateIdle);
    closeCamera();
    if (err == EOK) {
        err = openCamera((camera_unit_t)unit);
        if (err) {
            qDebug() << "failed to open camera" << unit;
        } else {
            err = setVfMode(mVfMode);
        }
        emit cameraUnitChanged((CameraUnit)mUnit);
    }
    return err;
}


int StreamCam::setVfMode(VfMode mode)
{
    int err = EOK;
    switch (mode) {
    case ModeVideo:
        mStopViewfinder = true;
        if (err == EOK) {
            err = runStateMachine(StateStartingVideoVf);
        } else {
            err = CAMERA_EALREADY;
        }
        break;
    default:
        err = runStateMachine(StateIdle);
        break;
    }
    if (err == EOK) {
        mVfMode = mode;
        if (mVfMode != ModeNone) {
            // remember last-set mode so that we can resume into that mode after minimize/powerdown
            mResumeVfMode = mVfMode;
        }
        emit vfModeChanged(mVfMode);
    }
    return err;
}


void StreamCam::inventoryCameras()
{
    unsigned int num;
    mHasFrontCamera = mHasRearCamera = false;
    if (camera_get_supported_cameras(0, &num, NULL) != EOK) {
        qDebug() << "failed to query number of supported cameras";
    } else {
        camera_unit_t units[num];
        if (camera_get_supported_cameras(num, &num, units) != EOK) {
            qDebug() << "failed to query supported cameras";
        } else {
            for (unsigned int i=0; i<num; i++) {
                if (units[i] == CAMERA_UNIT_FRONT) {
                    mHasFrontCamera = true;
                    if(!getStoredSupportedResolutions(CAMERA_UNIT_FRONT))
                        retrieveSupportedResolutions(CAMERA_UNIT_FRONT);
                } else if (units[i] == CAMERA_UNIT_REAR) {
                    mHasRearCamera = true;
                    if(!getStoredSupportedResolutions(CAMERA_UNIT_REAR))
                        retrieveSupportedResolutions(CAMERA_UNIT_REAR);
                }
            }
        }
    }
    emit hasFrontCameraChanged(mHasFrontCamera);
    emit hasRearCameraChanged(mHasRearCamera);
}


bool StreamCam::getStoredSupportedResolutions(camera_unit_t unit)
{
    QSettings settings("ShowStopper", "StreamCam");
    if(!settings.value(QString("Camera[%1]").arg((int)unit)).toString().isEmpty()) {
        QStringList list = settings.value(QString("Camera[%1]").arg((int)unit)).toString().split(";");
        QList<QSize>mSizes;
        foreach(QString s, list) {
            QStringList values = s.split("x");
            if(values.count()==2) {
                QSize size(values[0].toInt(), values[1].toInt());
                mSizes.append(size);
            }
        }
        if(unit==CAMERA_UNIT_FRONT)
            mFrontCamVideoResolutions = mSizes;
        else if(unit==CAMERA_UNIT_REAR)
            mRearCamVideoResolutions = mSizes;
        qDebug()<<"getStoredSupportedResolutions::Resolutions found!"<<unit;
        return true;
    }
    return false;
}

int StreamCam::retrieveSupportedResolutions(camera_unit_t unit)
{
    if(mHandle != CAMERA_HANDLE_INVALID) return -1;
    int err = camera_open((camera_unit_t)unit, CAMERA_MODE_RW | CAMERA_MODE_ROLL, &mHandle);
    if (err == EOK) {
        // now query the supported video resolutions
        unsigned int numVideoResolutions;
        camera_res_t* videoResolutions;
        err = camera_get_video_output_resolutions(mHandle, 0, &numVideoResolutions, NULL);
        if (err) {
            qDebug() << "retrieveSupportedResolutions::failed to query num video resolutions";
        } else {
            // now allocate enough storage to hold the array
            videoResolutions = new camera_res_t[numVideoResolutions];
            if (!videoResolutions) {
                qDebug() << "retrieveSupportedResolutions::failed to allocate storage for video resolutions array";
                err = ENOMEM;
            } else {
                // now fill the array
                err = camera_get_video_output_resolutions(mHandle,
                                                          numVideoResolutions,
                                                          &numVideoResolutions,
                                                          videoResolutions);
                if (err) {
                    qDebug() << "retrieveSupportedResolutions::failed to query video resolutions";
                } else {
                    QList<QSize> sizes;
                    // log the list
                    qDebug() << "retrieveSupportedResolutions::supported video resolutions:";
                    for (unsigned int i=0; i<numVideoResolutions; i++) {
                        QSize size(videoResolutions[i].width,videoResolutions[i].height);
                        qDebug() << videoResolutions[i].width << "x" << videoResolutions[i].height;
                        sizes.append(size);
                    }
                    if(unit==CAMERA_UNIT_FRONT)
                        mFrontCamVideoResolutions = sizes;
                    else if(unit==CAMERA_UNIT_REAR)
                        mRearCamVideoResolutions = sizes;
                    storeSupportedResolutions(unit, sizes);
                }
            }
        }
    }
    if (mHandle == CAMERA_HANDLE_INVALID) {
        qDebug() << "retrieveSupportedResolutions::no camera to close. ignoring.";
    } else {
        qDebug() << "retrieveSupportedResolutions::closing camera";
        camera_close(mHandle);
        mHandle = CAMERA_HANDLE_INVALID;
    }
    return err;
}

void StreamCam::storeSupportedResolutions(camera_unit_t unit, QList<QSize>sizes)
{
    if(sizes.isEmpty()) return;
    QString value;
    foreach(QSize size, sizes) {
        value += tr("%1x%2;").arg(size.width()).arg(size.height());
    }
    QSettings settings("ShowStopper", "StreamCam");
    settings.setValue(QString("Camera[%1]").arg((int)unit), value);
    qDebug()<<"getStoredSupportedResolutions::Resolutions stored!"<<unit;
}

int StreamCam::startVideoVf()
{
    int err = EOK;
    if(mVideoBitrate<128 || mVideoBitrate>3600)
        mVideoBitrate = 360;
    if(mVideoFramerate<15.0 || mVideoFramerate>30.0)
        mVideoFramerate = 30.0;
    mRequireUprightVf = false;      // since we are not processing pixels, we don't care which way the vf buffer is oriented.
                                    // however, when it comes time to record video, we will need to change this if the video source is the vf.
    mRequireUprightCapture = true;  // capture buffers must be upright since we don't have a metadata solution at this time
    // find the window group & window id required by the ForeignWindowControl
    QByteArray groupBA = mFwc->windowGroup().toLocal8Bit();
    QByteArray winBA = mFwc->windowId().toLocal8Bit();
    QSize vfSize = getCameraSizeFromPreferredSize(mNumVideoResolutionsList, mPreferredCameraSize);
    qDebug()<<"Actual camera video/preview size"<<vfSize;
    mPreferredCameraSize = vfSize;
    saveSettingsData(KEY_VIDEO_RESOLUTION, tr("%1x%2").arg(vfSize.width()).arg(vfSize.height()));
    saveSettingsData(KEY_VIDEO_BITRATE, mVideoBitrate);
    saveSettingsData(KEY_VIDEO_FRAMERATE, mVideoFramerate);
    emit outputWidthChanged();
    emit outputHeightChanged();
    if(!vfSize.isEmpty() && vfSize.isValid())
        err = camera_set_videovf_property(mHandle,
                                          CAMERA_IMGPROP_WIN_GROUPID, groupBA.data(),
                                          CAMERA_IMGPROP_WIN_ID, winBA.data(),
                                          CAMERA_IMGPROP_WIDTH, vfSize.width(),
                                          CAMERA_IMGPROP_HEIGHT, vfSize.height());
    else
        err = camera_set_videovf_property(mHandle,
                                          CAMERA_IMGPROP_WIN_GROUPID, groupBA.data(),
                                          CAMERA_IMGPROP_WIN_ID, winBA.data());
    if (err) {
        qDebug() << "error setting videovf properties:" << err;
    } else {
        if(!vfSize.isEmpty() && vfSize.isValid())
            err = camera_set_video_property(mHandle,
                                            CAMERA_IMGPROP_WIDTH, vfSize.width(),
                                            CAMERA_IMGPROP_HEIGHT, vfSize.height(),
                                            CAMERA_IMGPROP_ROTATION, 0,
                                            CAMERA_IMGPROP_FRAMERATE, mVideoFramerate,
                                            CAMERA_IMGPROP_VIDEOCODEC, CAMERA_VIDEOCODEC_H264,
                                            CAMERA_IMGPROP_AUDIOCODEC, CAMERA_AUDIOCODEC_AAC);
        else
            err = camera_set_video_property(mHandle,
        //                                    CAMERA_IMGPROP_WIDTH, vfSize.width(),
        //                                    CAMERA_IMGPROP_HEIGHT, vfSize.height(),
                                            CAMERA_IMGPROP_ROTATION, 0,
                                            CAMERA_IMGPROP_FRAMERATE, mVideoFramerate,
                                            CAMERA_IMGPROP_VIDEOCODEC, CAMERA_VIDEOCODEC_H264,
                                            CAMERA_IMGPROP_AUDIOCODEC, CAMERA_AUDIOCODEC_AAC);
        if (err != CAMERA_EOK) {
            qDebug() << " Could not set video property";
            return err;
        }
        err = camera_set_videoencoder_parameter(mHandle,
                       CAMERA_H264AVC_BITRATE, mVideoBitrate*1000,
                       CAMERA_H264AVC_KEYFRAMEINTERVAL, 30,
                       CAMERA_H264AVC_RATECONTROL, CAMERA_H264AVC_RATECONTROL_CBR,
                       CAMERA_H264AVC_PROFILE, CAMERA_H264AVC_PROFILE_HIGH,
                       CAMERA_H264AVC_LEVEL, CAMERA_H264AVC_LEVEL_4 );
        if(err != CAMERA_EOK) {
            qDebug() << " Could not set video encoder property";
            return err;
        }
        err = camera_init_video_encoder();
        if(err != CAMERA_EOK) {
            qDebug() << "Could not init video encoder!";
            return err;
        }
        err = camera_start_video_viewfinder(mHandle,
                                            NULL,
                                            NULL,
                                            NULL);
        if (err) {
            qDebug() << "error starting video viewfinder:" << err;
        } else {
            // let's force the screen to stay awake as long as our app is visible
            mApp->mainWindow()->setScreenIdleMode(ScreenIdleMode::KeepAwake);
            // TODO: move this to the smart config section
            camera_get_videovf_property(mHandle,
                                        CAMERA_IMGPROP_WIDTH, &mVfWidth,
                                        CAMERA_IMGPROP_HEIGHT, &mVfHeight);

            // since aspect ratio is only updated on a display size change, and we may be changing viewfinder
            // resolutions in a photo<->video switch, we need to kick the code to adjust the size onscreen
            constrainViewfinderAspectRatio();
        }
    }

    return err;
}

int StreamCam::stopVideoVf()
{
    qDebug() << "stopping video viewfinder";
    int err = camera_stop_video_viewfinder(mHandle);
    if (err) {
        qDebug() << "error trying to shut down video viewfinder:" << err;
    }
    mApp->mainWindow()->setScreenIdleMode(ScreenIdleMode::Normal);
    return err;
}

int StreamCam::startRecording(bool isStartStreaming)
{
    int err = EOK;

    // THE CAMERA SERVICE DOES NOT PLAY SOUNDS WHEN PICTURES ARE TAKEN OR
    // VIDEOS ARE RECORDED.  IT IS THE APP DEVELOPER'S RESPONSIBILITY TO
    // PLAY AN AUDIBLE SHUTTER SOUND WHEN A PICTURE IS TAKEN AND WHEN VIDEO
    // RECORDING STARTS AND STOPS.  NOTE THAT WHILE YOU MAY CHOOSE TO MUTE
    // SUCH SOUNDS, YOU MUST ENSURE THAT YOUR APP ADHERES TO ALL LOCAL LAWS
    // OF REGIONS WHERE IT IS DISTRIBUTED.  FOR EXAMPLE, IT IS ILLEGAL TO
    // MUTE OR MODIFY THE SHUTTER SOUND OF A CAMERA APPLICATION IN JAPAN OR
    // KOREA.
    // TBD:
    //   BlackBerry will be providing clarification of this policy as part of the
    //   NDK developer agreement and App World guidelines.  A link will
    //   be provided when the policy is publicly available.

    // NOTE: we use the _blocking variant here so that the sound doesn't bleed over
    // into our recording.  An alternate solution may involve muting the mic temporarily,
    // in order to allow video recording to start slightly sooner.
    soundplayer_play_sound_blocking("event_recording_start");

    if (camera_has_feature(mHandle, CAMERA_FEATURE_PREVIEWISVIDEO)) {
        // if the video output uses the same buffers as the viewfinder, then they must both be oriented the same if required
        mRequireUprightVf = mRequireUprightCapture;
        // need to update vf angle to match recording angle
        mDesiredVfAngle = mDesiredCapAngle;
    }
    // apply the changes
    updateVideoAngle();
    // flush our screen context to make changes take effect
    screen_window_t win = mFwc->windowHandle();
    if (!win) {
        qDebug() << "can't get window handle to flush context";
    } else {
        screen_context_t screen_ctx;
        screen_get_window_property_pv(win, SCREEN_PROPERTY_CONTEXT, (void **)&screen_ctx);
        screen_flush_context(screen_ctx, 0);
        //Start controller first so it will be ready to handle media frames
        if(isStartStreaming)
            emit streamingStart();
        err = camera_start_encode(mHandle,
                &video_callback,
                &enc_video_callback,
                &enc_audio_callback,
                &status_callback,
                (void*)this);
        mVideoFileDescriptor = 100; //Randomly assigned
        if (err != EOK) {
            qDebug() << "failed to start recording";
            if(isStartStreaming)
                emit streamingStop();
            return err;
        }
        qDebug()<<"Encoding started successfully!"<<isStartStreaming;
    }
    return err;
}


int StreamCam::stopRecording(bool isStopStreaming)
{
    int err = EOK;

    // revert to not requiring the viewfinder be upright, since we are not using the pixels from the buffer anymore
    mRequireUprightVf = false;

    // if a recording is in progress...
    if (mVideoFileDescriptor != -1) {
        // stop recording.
        err = camera_stop_encode(mHandle);
        if (err != EOK) {
            qDebug() << "failed to stop video recording. err " << err;
        }
        qDebug()<<"Encoding stopped successfully"<<isStopStreaming;
        if(isStopStreaming)
            emit streamingStop();
        mVideoFileDescriptor = -1;
    }

    // play the stop/abort tone
    soundplayer_play_sound("event_recording_stop");

    return err;
}

int StreamCam::runStateMachine(CamState newState)
{
    int err = EOK;
    CamState nextState;
    // try to enter the requested state
    while (mState != newState) {
        qDebug() << "exiting state" << stateName(mState);
        err = exitState();
        if (err != EOK) {
            return err;
        }

        mState = newState;

        qDebug() << "entering state" << stateName(newState);
        err = enterState(newState, nextState);
        if (err != EOK) {
            qDebug() << "error" << err << "entering state" << stateName(newState);
            // in error case, we will actually just move to idle
        }
        // check if a new transition was requested by attempting to enter state
        if (nextState != newState) {
            newState = nextState;
        }
    }
    return err;
}


int StreamCam::exitState()
{
    int err = EOK;
    switch(mState) {
    case StateIdle:
        // update UI?
        break;
    case StateVideoCapture:
        // unlock orientation when video recording ends
        OrientationSupport::instance()->setSupportedDisplayOrientation(SupportedDisplayOrientation::All);
        err = stopRecording(!mSwitchingCamera);
        if (mStopViewfinder) {
            err = stopVideoVf();
        }
        if(!mSwitchingCamera)
            emit capturingChanged(mCapturing = false);
        break;
    case StateStartingVideoVf:
    case StateVideoVf:
        // update UI
        emit canCaptureChanged(mCanCapture = false);
        if (mStopViewfinder) {
            err = stopVideoVf();
        }
        break;

    default:
        // nothing to do when exiting other states
        break;
    }

    return err;
}


int StreamCam::enterState(CamState state, CamState &nextState)
{
    // unless otherwise told, we will be remaining in the requested state
    nextState = state;
    int err = EOK;

    switch(state) {
    case StateIdle:
        // update UI
        mCanCapture = false;
        canCaptureChanged(mCanCapture);
        break;
    case StateStartingVideoVf:
        err = startVideoVf();
        if (err) {
            nextState = StateIdle;
        } else {
            mStopViewfinder = true;
        }
        break;
    case StateVideoVf:
        mStopViewfinder = true;
        updateAngles();
        // update UI
        if(!mSwitchingCamera)
            emit canCaptureChanged(mCanCapture = true);
        break;
    case StateVideoCapture:
        // lock orientation while video recording
        OrientationSupport::instance()->setSupportedDisplayOrientation(SupportedDisplayOrientation::CurrentLocked);
        emit capturingChanged(mCapturing = true);
        err = startRecording(!mSwitchingCamera);
        if (err) {
            nextState = StateVideoVf;
        } else {
            emit canCaptureChanged(mCanCapture = true);
        }
        break;
    case StatePowerDown:
    case StateMinimized:
        // NOTE: we are combining powerdown and minimized states here for now, as we are treating them the same.
        // We are also going to be closing the camera in this state in order to play nice with other apps.
        closeCamera();
        emit vfModeChanged(mVfMode = ModeNone);
        break;
    default:
        // nothing to do?
        break;
    }

    // if we have just entered a state and resource warning is pending
    // (eg. we were in the middle of a video capture when the warning was received),
    // then deal with it now
    if (mDeferredResourceWarning) {
        mDeferredResourceWarning = false;
        // TODO: which state is the best one in this case? Idle? PowerDown?
        nextState = StatePowerDown;
    }

    return err;
}


const char* StreamCam::stateName(CamState state)
{
    switch(state) {
    case StateIdle:
        return "Idle";
    case StateStartingVideoVf:
        return "StartingVideoVf";
    case StateVideoVf:
        return "VideoVf";
    case StateVideoCapture:
        return "VideoCapture";
    case StatePowerDown:
        return "PowerDown";
    case StateMinimized:
        return "Minimized";
    default:
        return "UNKNOWN";
    }
}


int StreamCam::windowAttached()
{
    int err = EOK;

    // update window details
    screen_window_t win = mFwc->windowHandle();
    // put the viewfinder window behind the cascades window
    int i = -1;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &i);

    CamState newState = StateIdle;
    mStopViewfinder = true;
    switch (mState) {
    case StateStartingVideoVf:
        // ensure we don't stop the viewfinder when transitioning out of StateStartingVideoVf
        mStopViewfinder = false;
        updateAngles();
        updateVideoAngle();
        newState = StateVideoVf;
        break;
    default:
        qDebug() << "unexpected window attach while not waiting for one";
        emit vfModeChanged(mVfMode = ModeNone);
        err = EINVAL;
        break;
    }

    screen_context_t screen_ctx;
    screen_get_window_property_pv(win, SCREEN_PROPERTY_CONTEXT, (void **)&screen_ctx);
    screen_flush_context(screen_ctx, 0);

    err = runStateMachine(newState);
    return err;
}

StatusThread::StatusThread(StreamCam* cam) :
    QThread(),
    mCam(cam),
    mStop(false)
{
    mHandle = mCam->mHandle;
    mChId = ChannelCreate(0);
    mCoId = ConnectAttach(0, 0, mChId, _NTO_SIDE_CHANNEL, 0);
    SIGEV_PULSE_INIT(&mEvent, mCoId, SIGEV_PULSE_PRIO_INHERIT, mPulseId, 0);
}


void StatusThread::run()
{
    int err = EOK;
    int rcvid;
    struct _pulse pulse;
    camera_eventkey_t key;
    camera_devstatus_t status;
    uint16_t extra;
    err = camera_enable_status_event(mHandle, &key, &mEvent);
    if (err) {
        qDebug() << "could not enable status event. err =" << err;
    } else {
        qDebug() << "status thread running";
        while(!mStop) {
            rcvid = MsgReceive(mChId, &pulse, sizeof(pulse), NULL);
            // not a pulse?
            if (rcvid != 0) continue;
            // not our pulse?
            if (pulse.code != mPulseId) continue;
            // instructed to stop?
            if (mStop) break;
            err = camera_get_status_details(mHandle, pulse.value, &status, &extra);
            if (err) {
                qDebug() << "failed to get status event details??";
            } else {
                emit statusChanged(status, extra);
            }
        }
        camera_disable_event(mHandle, key);
    }
    qDebug() << "status thread exiting";
}


void StatusThread::cleanShutdown()
{
    mStop = true;
    MsgSendPulse(mCoId, -1, mPulseId, 0);
}


void StreamCam::onStatusChanged(camera_devstatus_t status, uint16_t extra)
{
    //qDebug() << "status event:" << status << "," << extra;
    switch(status) {
    case CAMERA_STATUS_RESOURCENOTAVAIL:
        qDebug() << "camera resources are about to become unavailable";
        resourceWarning();
        break;
    case CAMERA_STATUS_POWERDOWN:
        qDebug() << "camera powered down";
        poweringDown();
        break;
    case CAMERA_STATUS_POWERUP:
        qDebug() << "camera powered up";
        // since the onFullscreen handler will restart the camera for us, there is not much to do here.
        // however, if we were the sort of app that wanted to start running again even if we were backgrounded,
        // then we could consider resuming the viewfinder here.
        break;
    default:
        break;
    }
    // suppress warning
    (void)extra;
}


void StreamCam::onFullscreen()
{
    qDebug() << "onFullscreen()";
    switch(mState) {
    case StateMinimized:
    case StatePowerDown:  // TODO: does this one need to be different?
        // coming back to the foreground, resume viewfinder
        runStateMachine(StateIdle);
        setCameraUnit((CameraUnit)mUnit);
        qDebug() << "setting vf mode" << mResumeVfMode;
        setVfMode(mResumeVfMode);
        break;
    default:
        // nothing to do
        break;
    }
}


void StreamCam::onThumbnail()
{
    qDebug() << "onThumbnail()";
    switch (mState) {
    case StateVideoCapture:
        // if we are recording a video when we get minimized, let's keep recording
        qDebug() << "ignoring thumbnail signal... keep running!";
        break;
    default:
        runStateMachine(StateMinimized);
        break;
    }
}


void StreamCam::onInvisible()
{
    qDebug() << "onInsivible()";
    switch (mState) {
    case StateVideoCapture:
        // if we are recording a video when we get covered, let's keep recording.
        // NOTE: since the app is no longer visible on-screen, the ScreenIdleMode::KeepAwake setting
        // will not be enough to keep the device from shutting down.. that's alright, because when the
        // video recorder (or encoder) is active, the OS will prevent the device from going into standby automatically.
        qDebug() << "ignoring invisible signal... keep running!";
        break;
    default:
        // not really treating thumbnail/invisible differently.  could have connected both signals to a single slot really.
        runStateMachine(StateMinimized);
        break;
    }
}


void StreamCam::resourceWarning()
{
    switch (mState) {
    default:
        runStateMachine(StatePowerDown);
        break;
    }
}


void StreamCam::poweringDown()
{
    runStateMachine(StatePowerDown);
}


void StreamCam::onDisplayDirectionChanging(bb::cascades::DisplayDirection::Type displayDirection,
                                         bb::cascades::UIOrientation::Type orientation)
{
    qDebug() << "onDisplayOrientationChange()";

    // this will only be called when supported orientations are activated.
    // on the Q10, there is only one official orientation, and on most other devices, 180 degrees
    // is not supported.  we have to also hook into the orientation sensor api in order to cover all angles
    // on all devices.
    qDebug() << "display direction change:" << displayDirection;
    // note: this was only a vf orientation change event, so leave the QOrientationSensor reading as its last cached value...
    mDisplayDirection = displayDirection;
    updateAngles();
    // silence compiler warning
    (void)orientation;
}


void StreamCam::updateAngles()
{
    // For a camera facing in the same direction as the user (forward), the desired display angle is the complement
    // of the display direction.  This is because the nav display direction signals are reported as:
    // "the edge of the device which is now topmost".  In the camera's space, we report
    // angles as clockwise rotations of a buffer, or of the device.  So if nav reports that "90" is the new
    // display direction, that means that the 90-degree-clockwise edge (or 3-o'clock edge) of the device is
    // now topmost.  that means that the device has actually been rotated 90 degrees counterclockwise.
    // 90 degrees counterclockwise is equivalent to 270 degrees clockwise.  We want our picture to be rotated by
    // the same amount (270 degrees clockwise), therefore we use the complement of navigator's reported "90".
    // Here is an important distinction...
    // For a camera which faces in the opposite direction as the user (eg. backwards - towards the user), the
    // angle that the device is being rotated needs to be reversed.  This is because from the camera's perspective
    // (standing behind the device, facing the user), the device has been rotated in the opposite direction from that which
    // the user would perceive.  Once you understand this distinction, the front/rear decisions below will make more sense.
    // Although confusingly, CAMERA_UNIT_REAR is the camera facing in the same direction as the user (it faces out
    // the rear of the device), and CAMERA_UNIT_FRONT is the camera facing towards the user.

    // here, I will reverse the nav's reported rotation, to bring it in line with the screen and camera's
    // co-ordinate reference.  eg. turning the device clockwise yields a rotation of 90 (nav reports 270)
    int clockwiseDisplayAngle = (360 - mDisplayDirection) % 360;
    int clockwiseOrientationAngle = (360 - mOrientationDirection) % 360;

    // note that the device orientation is not dependent on which camera (front vs rear) we are using when used with
    // camera_set_device_orientation().  the distinction is performed by the camera service.
    mDeviceOrientation = clockwiseOrientationAngle;

    // now account for front/rear-facing camera reversal of rotation direction
    if (mUnit == CAMERA_UNIT_FRONT) {
        mDesiredVfAngle = (360 - clockwiseDisplayAngle) % 360;
        mDesiredCapAngle = (360 - clockwiseOrientationAngle) % 360;
    } else {
        mDesiredVfAngle = clockwiseDisplayAngle;
        mDesiredCapAngle = clockwiseOrientationAngle;
    }

    qDebug() << "display direction:" << mDisplayDirection
             << "orientation direction:" << mOrientationDirection
             << "desired vf angle:" << mDesiredVfAngle
             << "desired cap angle:" << mDesiredCapAngle
             << "device orientation: " << mDeviceOrientation;

    // now that we know which way is up, let's decide if we need to do anything about it.
    switch (mState) {
    case StateVideoVf:
        updateVideoAngle();
        break;
    case StateVideoCapture:
        // we can't change the angle while recording
        qDebug() << "In recording state, ignoring angle change";
        break;
    default:
        // we can't change the angle while taking a picture, or starting up, so may as well ignore.
        // TODO: could just set a deferred flag and deal with it on a state transition.
        qDebug() << "not in a stable viewfinder state, ignoring angle change";
        break;
    }
}

void StreamCam::updateVideoAngle()
{
    int i;
    int err = EOK;
    err = camera_get_videovf_property(mHandle, CAMERA_IMGPROP_ROTATION, &mVfAngle);

    if (mRequireUprightVf) {
        // if required, let's select a physical viewfinder buffer rotation which will result in an upright buffer.
        // if this is not possible, then we have to make up the difference with a screen rotation effect.
        // NOTE: that this is only typically required when we are starting video recording and CAMERA_FEATURE_PREVIEWISVIDEO is
        // asserted.  if we were processing viewfinder pixels and expecting the first one to be in the top-left position, then we
        // would also set this flag.
        // check whether the desired angle is available...
        for (i=0; i<mNumVideoVfRotations; i++) {
            if (mVideoVfRotations[i] == mDesiredVfAngle) break;
        }
        if (i == mNumVideoVfRotations) {
            qDebug() << "desired videovf angle" << mDesiredVfAngle << "is not available";
            // we'll have to rely on screen alone in this case.
        } else {
            err = camera_set_videovf_property(mHandle, CAMERA_IMGPROP_ROTATION, mDesiredVfAngle);
            if (err) {
                qDebug() << "failed to set videovf angle" << mDesiredVfAngle << "err:" << err;
            } else {
                mVfAngle = mDesiredVfAngle;
                qDebug() << "set videovf rotation" << mVfAngle;
            }
        }
    }

    if (mRequireUprightCapture) {
        // if required, let's select a physical capture buffer rotation which will result in an upright buffer.
        // today, all platforms support this in video mode, but in the future, we may need to rely on metadata in the video stream.
        err = camera_get_video_property(mHandle, CAMERA_IMGPROP_ROTATION, &mCaptureAngle);
        if (err) {
            qDebug() << "failed to query capture angle. err:" << err;
        } else {
            // check whether the desired angle is available...
            for (i=0; i<mNumVideoRotations; i++) {
                if (mVideoRotations[i] == mDesiredCapAngle) break;
            }
            if (i == mNumVideoRotations) {
                qDebug() << "desired video output angle" << mDesiredCapAngle << "is not available";
                // we'll have to rely on metadata alone in this case. (which we cannot do today, since there is no standard for mp4)
            } else {
                err = camera_set_video_property(mHandle, CAMERA_IMGPROP_ROTATION, mDesiredCapAngle);
                if (err) {
                    qDebug() << "failed to set video output angle" << mDesiredCapAngle << "err:" << err;
                } else {
                    mCaptureAngle = mDesiredCapAngle;
                    qDebug() << "set video output rotation" << mCaptureAngle;
                }
            }
        }
    }

    // compute screen display angle now that we know how the viewfinder buffers are being rotated.
    // remember: desired angle = viewfinder angle + window angle.  solve for viewfinder angle:
    mWindowAngle = (360 + mDesiredVfAngle - mVfAngle) % 360;  // note +360 to avoid negative numbers in modulo math

    if (mRequireUprightVf) {
        // NOTE: in the video case, since viewfinder buffers may be required to match the angle of the video buffers, we must apply a
        // correction offset here (which is the difference between the UI angle and the device orientation angle).  this is typically needed
        // when recording with the device held in an orientation where the UI cannot be rotated.  (eg. most angles on a Q10 or upside-down on a Z10).
        // also note that we are only applying this offset in the case where mRequireUprightVf is asserted.  this is to ensure that
        // this adjustment is only made when we are reconfiguring the viewfinder buffers during recording.
        // There is probably a less confusing way to orchestrate this series of corner-case events, but for now, this should be fine.
        uint32_t uiOffsetAngle = (360 + mOrientationDirection - mDisplayDirection) % 360;
        if (mUnit == CAMERA_UNIT_FRONT) {
            uiOffsetAngle = (360 - uiOffsetAngle) % 360;
        }
        mWindowAngle = (mWindowAngle + uiOffsetAngle) % 360;
    }

    // all we need to do here is update the screen window associated with the viewfinder.
    screen_window_t win = mFwc->windowHandle();
    if (!win) {
        qDebug() << "no window handle available to update";
    } else {
        screen_set_window_property_iv(win, SCREEN_PROPERTY_ROTATION, (int*)&mWindowAngle);
        int mirror = 0;
        int flip = 0;
        if (mUnit == CAMERA_UNIT_FRONT) {
            // NOTE: since mirroring is applies after rotation in the order-of-operations, it is necessary to
            // decide between a flip or a mirror in order to make the screen behave like a mirror on the front camera.
            if (mWindowAngle % 180) {
                flip = 1;
            } else {
                mirror = 1;
            }
            screen_set_window_property_iv(win, SCREEN_PROPERTY_MIRROR, &mirror);
            screen_set_window_property_iv(win, SCREEN_PROPERTY_FLIP, &flip);
        }
    }

    // always tell camera which way device is oriented.
    // even though we don't have any metadata tags which can be used with video recordings, telling the camera which way is up
    // helps to optimize exposure profiles and aids the face detection algorithms (which only detect upright faces).
    err = camera_set_device_orientation(mHandle, mDeviceOrientation);
    if (err) {
        qDebug() << "failed to set camera device orientation to" << mDeviceOrientation <<" err:" << err;
    } else {
        qDebug() << "camera device orientation set to" << mDeviceOrientation;
    }
    emit outputWidthChanged();
    emit outputHeightChanged();
}


int StreamCam::startOrientationReadings()
{
    if (!mOrientationSensor) {
        mOrientationSensor = new QOrientationSensor(this);
        if (!mOrientationSensor) {
            qWarning() << "failed to allocate QOrientationSensor.";
            return ENOMEM;
        }
        mOrientationSensor->setSkipDuplicates(true);
        mOrientationSensor->setDataRate(1);
        mOrientationSensor->setAlwaysOn(true);
        if (!QObject::connect(mOrientationSensor, SIGNAL(readingChanged()), this, SLOT(onOrientationReadingChanged()))) {
            qWarning() << "failed to connect readingChanged signal";
            return EIO;
        }
    }
    mOrientationSensor->start();
    return EOK;
}


void StreamCam::stopOrientationReadings()
{
    if (mOrientationSensor) {
        mOrientationSensor->stop();
    }
}


void StreamCam::onOrientationReadingChanged()
{
    if (!mOrientationSensor) {
        return;
    }
    qDebug() << "onOrientationReadingChanged()";
    QOrientationReading* reading = mOrientationSensor->reading();
    if (reading) {
        switch(reading->orientation()) {
        case QOrientationReading::TopUp:
            mOrientationDirection = DisplayDirection::North;
            break;
        case QOrientationReading::TopDown:
            mOrientationDirection = DisplayDirection::South;
            break;
        case QOrientationReading::LeftUp:
            mOrientationDirection = DisplayDirection::West;
            break;
        case QOrientationReading::RightUp:
            mOrientationDirection = DisplayDirection::East;
            break;
        default:
            // this is an unhandled direction (eg. face-up or face-down), so just reuse the last known reading
            break;
        }
        // note: this was only a QOrientationSensor change event, so leave the UI display direction reading at its last known value...
        updateAngles();
    }
}


void StreamCam::onVfParentLayoutFrameChanged(QRectF frame)
{
    qDebug() << "viewfinder parent size:" << frame;
    // by default, the ForeignWindowControl that houses the viewfinder will scale to fit the available screen real-estate.
    // this will likely lead to it being stretched in one direction.
    // we're going to un-stretch it, and peg it's aspect ratio at 16:9 (or 9:16).
    mVfContainerSize = frame;
    constrainViewfinderAspectRatio();
}


void StreamCam::constrainViewfinderAspectRatio()
{
    if ((mVfContainerSize.width() == 0) ||
        (mVfContainerSize.height() == 0)) {
        // one of the dimensions is a zero, not wise to do math with this yet
        return;
    }

    // first, determine whether we are aiming for a portrait or landscape aspect ratio (eg. 9:16 or 16:9)
    float aspect = (float)mVfWidth / (float)mVfHeight;
    // if window is displayed at 90 or 270 degrees, then flip the target aspect ratio
    if (mDesiredVfAngle % 180) {
        aspect = 1 / aspect;
    }

    // until we figure otherwise, fit to max size
    float width = mVfContainerSize.width();
    float height = mVfContainerSize.height();

    if (height * aspect > width) {
        // constrain width
        float w = height * aspect;
        if(w<1.1*width)
            width = w;
        else
            height = width / aspect;
    } else {
        // constrain height, since width cannot be increased
        float h = width / aspect;
        if(h<1.1*height)
            height = h;
        else
            width = height * aspect;
    }
    mFwc->setMinWidth(width);
    mFwc->setMaxWidth(height);
    mFwc->setMinHeight(height);
    mFwc->setMaxHeight(height);
    qDebug() << "resized viewfinder to" << width << "x" << height <<"to maintain aspect ratio" << aspect;
}


int StreamCam::discoverCameraCapabilities()
{
    // In this function, we will query and cache some core camera capabilities to use for later configuration.
    // 1. video?
    // 2. video format, rotations, resolutions
    // 3. video viewfinder format, rotations, resolutions


    // first check for video support
    mCanDoVideo = false;
    if (camera_has_feature(mHandle, CAMERA_FEATURE_VIDEO)) {
        mCanDoVideo = true;
    }
    emit canDoVideoChanged(mCanDoVideo);

    int err = EOK;

    if (mCanDoVideo) {
        err = discoverVideoCapabilities();
        if (err) {
            qDebug() << "failed to discover video capabilities.";
            return err;
        }
        err = discoverVideoVfCapabilities();
        if (err) {
            qDebug() << "failed to discover videovf capabilities.";
            return err;
        }
    }

    return err;
}

int StreamCam::discoverVideoCapabilities()
{
    int err = EOK;
    // clean up any pre-discovered stuff
    delete[] mVideoRotations;
    delete[] mVideoResolutions;
    mNumVideoRotations = 0;
    mNumVideoResolutions = 0;

    // now query the current format for video capture.  in this sample, we are not implementing configurable formats,
    // however we should make sure to know what the default is so that we can query some other discovery functions.
    // TODO: this is embarrassing.. apparently the video property is not queryable presently. that is okay, since on all
    // current platforms, the video and videovf streams are the same.  we can add a check here and query the videovf as a workaround.
    if (camera_has_feature(mHandle, CAMERA_FEATURE_PREVIEWISVIDEO)) {
        err = camera_get_videovf_property(mHandle, CAMERA_IMGPROP_FORMAT, &mVideoFormat);
    } else {
        err = camera_get_video_property(mHandle, CAMERA_IMGPROP_FORMAT, &mVideoFormat);
    }
    if (err) {
        qDebug() << "failed to query video format";
    } else {
        // log it.
        qDebug() << "current video format is:" << mVideoFormat;

        // now query which buffer rotations are available for this format.
        // since we don't know how large the list may be (technically it shouldn't be more than 4 entries), we can
        // query the function in pre-sizing mode -- eg. by providing numasked=0 and a NULL array).
        err = camera_get_video_rotations(mHandle, mVideoFormat, 0, &mNumVideoRotations, NULL, NULL);
        if (err) {
            qDebug() << "failed to query num video rotations";
        } else {
            // now allocate enough storage to hold the array
            mVideoRotations = new uint32_t[mNumVideoRotations];
            if (!mVideoRotations) {
                qDebug() << "failed to allocate storage for video rotations array";
                err = ENOMEM;
            } else {
                // now fill the array
                err = camera_get_video_rotations(mHandle,
                                                 mVideoFormat,
                                                 mNumVideoRotations,
                                                 &mNumVideoRotations,
                                                 mVideoRotations,
                                                 NULL);
                if (err) {
                    qDebug() << "failed to query video rotations";
                } else {
                    // log the list.
                    qDebug() << "supported video rotations:";
                    for (int i=0; i<mNumVideoRotations; i++) {
                        qDebug() << mVideoRotations[i];
                    }
                }
            }
        }
    }

    if (err == EOK) {
        // now query the supported video resolutions
        err = camera_get_video_output_resolutions(mHandle, 0, &mNumVideoResolutions, NULL);
        if (err) {
            qDebug() << "failed to query num video resolutions";
        } else {
            // now allocate enough storage to hold the array
            mVideoResolutions = new camera_res_t[mNumVideoResolutions];
            if (!mVideoResolutions) {
                qDebug() << "failed to allocate storage for video resolutions array";
                err = ENOMEM;
            } else {
                // now fill the array
                err = camera_get_video_output_resolutions(mHandle,
                                                          mNumVideoResolutions,
                                                          &mNumVideoResolutions,
                                                          mVideoResolutions);
                if (err) {
                    qDebug() << "failed to query video resolutions";
                } else {
                    // log the list
                    qDebug() << "supported video resolutions:";
                    for (unsigned int i=0; i<mNumVideoResolutions; i++) {
                        qDebug() << mVideoResolutions[i].width << "x" << mVideoResolutions[i].height;
                    }
                }
            }
        }
    }

    return err;
}


int StreamCam::discoverVideoVfCapabilities()
{
    int err = EOK;
    // clean up any pre-discovered stuff
    delete[] mVideoVfRotations;
    delete[] mVideoVfResolutions;
    mNumVideoVfRotations = 0;
    mNumVideoVfResolutions = 0;

    // now query the current format for video viewfinder.  in this sample, we are not implementing configurable formats,
    // however we should make sure to know what the default is so that we can query some other discovery functions.
    err = camera_get_videovf_property(mHandle, CAMERA_IMGPROP_FORMAT, &mVideoVfFormat);
    if (err) {
        qDebug() << "failed to query videovf format";
    } else {
        // log it.
        qDebug() << "current videovf format is:" << mVideoVfFormat;

        // now query which buffer rotations are available for this format.
        // since we don't know how large the list may be (technically it shouldn't be more than 4 entries), we can
        // query the function in pre-sizing mode -- eg. by providing numasked=0 and a NULL array).
        err = camera_get_video_vf_rotations(mHandle, mVideoVfFormat, 0, &mNumVideoVfRotations, NULL, NULL);
        if (err) {
            qDebug() << "failed to query num videovf rotations";
        } else {
            // now allocate enough storage to hold the array
            mVideoVfRotations = new uint32_t[mNumVideoVfRotations];
            if (!mVideoVfRotations) {
                qDebug() << "failed to allocate storage for videovf rotations array";
                err = ENOMEM;
            } else {
                // now fill the array
                err = camera_get_video_vf_rotations(mHandle,
                                                    mVideoVfFormat,
                                                    mNumVideoVfRotations,
                                                    &mNumVideoVfRotations,
                                                    mVideoVfRotations,
                                                    NULL);
                if (err) {
                    qDebug() << "failed to query videovf rotations";
                } else {
                    // log the list.
                    qDebug() << "supported videovf rotations:";
                    for (int i=0; i<mNumVideoVfRotations; i++) {
                        qDebug() << mVideoVfRotations[i];
                    }
                }
            }
        }
    }

    if (err == EOK) {
        // now query the supported video vf resolutions
        err = camera_get_video_vf_resolutions(mHandle, 0, &mNumVideoVfResolutions, NULL);
        if (err) {
            qDebug() << "failed to query num videovf resolutions";
        } else {
            // now allocate enough storage to hold the array
            mVideoVfResolutions = new camera_res_t[mNumVideoVfResolutions];
            if (!mVideoVfResolutions) {
                qDebug() << "failed to allocate storage for videovf resolutions array";
                err = ENOMEM;
            } else {
                // now fill the array
                err = camera_get_video_vf_resolutions(mHandle,
                                                      mNumVideoVfResolutions,
                                                      &mNumVideoVfResolutions,
                                                      mVideoVfResolutions);
                if (err) {
                    qDebug() << "failed to query videovf resolutions";
                } else {
                    // log the list
                    qDebug() << "supported videovf resolutions:";
                    for (unsigned int i=0; i<mNumVideoVfResolutions; i++) {
                        mNumVideoResolutionsList.append(QSize(mVideoVfResolutions[i].width,
                                mVideoVfResolutions[i].height));
                        qDebug() << mVideoVfResolutions[i].width << "x" << mVideoVfResolutions[i].height;
                    }
                }
            }
        }
    }

    return err;
}


camera_res_t* StreamCam::matchAspectRatio(camera_res_t* target, camera_res_t* resList, int numRes, float accuracy)
{
    // this function will scan the list (resList) for resolutions which match the input aspect ratio (target) within
    // a margin of error of accuracy %.  eg. 0% means only match exact aspect ratios.
    camera_res_t* best = NULL;
    if (target && resList && numRes) {
        float targetRatio = (float)(target->width) / (float)(target->height);
        float bestError = 0;
        for (int i=0; i<numRes; i++) {
            float thisRatio = (float)(resList[i].width) / (float)(resList[i].height);
            float thisError = fabs((thisRatio / targetRatio) - 1.0f);
            if (thisError <= accuracy) {
                if (!best || (thisError < bestError)) {
                    best = &resList[i];
                    bestError = thisError;
                }
            }
        }
    }
    return best;
}

QSize StreamCam::getCameraSizeFromPreferredSize(QList<QSize>sizes, QSize preferredSize) {
    qDebug()<<"S!zes"<<sizes.size()<<preferredSize<<sizes;
    if(sizes.isEmpty()) return QSize();
    int difference = qAbs(sizes.first().width()-preferredSize.width()) +
            qAbs(sizes.first().height()-preferredSize.height());
    int index = 0;
    for(int i=0;i<sizes.count();i++) {
        QSize s = sizes.at(i);
        int d = qAbs(s.width()-preferredSize.width()) +
                qAbs(s.height()-preferredSize.height());
        if(d<difference) {
            difference = d;
            index = i;
        }
    }
    return sizes.at(index);
}


void StreamCam::video_callback(camera_handle_t cameraHandle,
        camera_buffer_t* cameraBuffer,
        void* etc) {
//    qDebug()<<"----Video frame"<<cameraBuffer->frametimestamp
//                <<cameraBuffer->frametype;
}
void StreamCam::enc_video_callback(camera_handle_t cameraHandle,
        camera_buffer_t* cameraBuffer,
        void* etc) {
    camera_frame_compressedvideo_t ct = cameraBuffer->framedesc.compvid;
    ((StreamCam*)etc)->controller()->handleVideoFrame(cameraBuffer->framebuf,
                            ct.bufsize,
                            cameraBuffer->frametimestamp,
                            ct.keyframe);
//    uint8_t type = (cameraBuffer->framebuf[4] & 31);
//    qDebug()<<"----Encoded video frame"<<cameraBuffer->frametimestamp
//            <<type<<ct.bufsize<<ct.codec<<ct.keyframe;
}
void StreamCam::enc_audio_callback(camera_handle_t cameraHandle,
        camera_buffer_t* cameraBuffer,
        void* etc) {
    camera_frame_compressedaudio_t ct = cameraBuffer->framedesc.compaud;
    ((StreamCam*)etc)->controller()->handleAudioFrame(cameraBuffer->framebuf,
                                ct.bufsize,
                                cameraBuffer->frametimestamp,
                                ct.keyframe);
//    qDebug()<<"Encoded audio frame"<<cameraBuffer->frametimestamp<<ct.bufsize;
}
void StreamCam::status_callback(camera_handle_t cameraHandle,
        camera_devstatus_t devStatus,
        uint16_t data,
        void* etc) {

}

int StreamCam::outputWidthDisplay() {
    if((mDeviceOrientation/90)%2==0)
        return mPreferredCameraSize.width();
    else
        return mPreferredCameraSize.height();
}

int StreamCam::outputWidth() {
    return mPreferredCameraSize.width();
}

int StreamCam::outputHeightDisplay() {
    if((mDeviceOrientation/90)%2==0)
        return mPreferredCameraSize.height();
    else
        return mPreferredCameraSize.width();
}

int StreamCam::outputHeight() {
    return mPreferredCameraSize.height();
}

void StreamCam::prepareCameraResolutionsModel(camera_unit_t unit)
{
    if(mCameraResolutionsModel==NULL)
        mCameraResolutionsModel = new ArrayDataModel(this);
    mCameraResolutionsModel->clear();
    QList<QSize> currentList, otherList;
    QString other;
    if(unit==CAMERA_UNIT_FRONT) {
        currentList = mFrontCamVideoResolutions;
        otherList = mRearCamVideoResolutions;
        other = "rear";
    } else {
        currentList = mRearCamVideoResolutions;
        otherList = mFrontCamVideoResolutions;
        other = "front";
    }

    for(int i=0;i<currentList.size();++i) {
        QVariantMap item;
        QSize size = currentList[i];
        item["text"] = tr("%1x%2").arg(size.width()).arg(size.height());
        if(!otherList.isEmpty() && !otherList.contains(size)) {
            item["description"] = tr("Not supported by %1 camera").arg(other);
            if(size==mPreferredCameraSize)
                emit requestCameraSwitchabilityToggle(false);
        } else
            item["description"] = "";
        mCameraResolutionsModel->append(item);
    }
    emit cameraResolutionsChanged();
}

void StreamCam::setPreferredCameraSize(QString size)
{
    if(!size.isEmpty() &&
            size.contains("x") &&
            size.split("x").count()==2) {
        int w = size.split("x").first().toInt();
        int h = size.split("x").last().toInt();
        mPreferredCameraSize.setWidth(w);
        mPreferredCameraSize.setHeight(h);
        emit outputWidthChanged();
        emit outputHeightChanged();
    }
}


void StreamCam::restartCamera()
{
    if(mState == StateVideoVf) {
        int err = runStateMachine(StatePowerDown);
        if(err!=EOK) {
            qDebug()<<"Unable to stop vf!"<<err;
        } else
            QTimer::singleShot(512, this, SLOT(onFullscreen()));
    }
}

void StreamCam::on_mController_publishError(const QString error)
{
    qDebug()<<"StreamCam-PublishError"<<error;
    //Stop camera encoding
    capture();
    emit streamingError(error);
}

void StreamCam::saveSettingsData(const QString key, const QVariant value)
{
    QSettings settings("ShowStopper", "StreamCam");
    settings.setValue(key, value);
}
