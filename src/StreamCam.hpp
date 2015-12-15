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

#ifndef __StreamCam_HPP__
#define __StreamCam_HPP__

#include <QObject>
#include <QThread>
#include <camera/camera_api.h>
#include <bb/cascades/DisplayDirection>
#include <bb/cascades/UIOrientation>
#include <QRectF>
#include <QSize>
#include <bb/cascades/ArrayDataModel>
#include "controller.h"

#define KEY_VIDEO_BITRATE "Video_Bitrate"
#define KEY_VIDEO_FRAMERATE "Video_Framerate"
#define KEY_VIDEO_RESOLUTION "Video_Resolution"


namespace bb
{
    namespace cascades
    {
        class Application;
        class LocaleHandler;
        class AbstractPane;
        class ForeignWindowControl;
        class ArrayDataModel;
    }
}

namespace QtMobility
{
    class QOrientationSensor;
}

class QTranslator;
class StreamCam;

/*!
 * @brief Application object
 *
 *
 */

class StatusThread : public QThread {
    Q_OBJECT
public:
    StatusThread(StreamCam* cam);
    void run();
    void cleanShutdown();
signals:
    void statusChanged(camera_devstatus_t, uint16_t);
private:
    StreamCam* mCam;
    camera_handle_t mHandle;
    bool mStop;
    int mChId;
    int mCoId;
    struct sigevent mEvent;
    static const int mPulseId = 123;
};


class StreamCam : public QObject
{
    Q_OBJECT
public:
    StreamCam(bb::cascades::Application *app);
    virtual ~StreamCam() {}

    static void video_callback(camera_handle_t cameraHandle,
            camera_buffer_t* cameraBuffer,
            void* etc);
    static void enc_video_callback(camera_handle_t cameraHandle,
            camera_buffer_t* cameraBuffer,
            void* etc);
    static void enc_audio_callback(camera_handle_t cameraHandle,
            camera_buffer_t* cameraBuffer,
            void* etc);
    static void status_callback(camera_handle_t cameraHandle,
            camera_devstatus_t devStatus,
            uint16_t data,
            void* etc);

    friend class StatusThread;  // TODO - formalize interface & remove friendship

    Q_ENUMS(VfMode);
    Q_ENUMS(CameraUnit);

    // an enum we can expose to QML
    enum CameraUnit {
        UnitNone = CAMERA_UNIT_NONE,
        UnitFront = CAMERA_UNIT_FRONT,
        UnitRear = CAMERA_UNIT_REAR
    };

    enum VfMode {
        ModeNone = 0,
        ModeVideo,
    };

    enum CamState {
        StateIdle = 0,
        StateStartingVideoVf,
        StateVideoVf,
        StateVideoCapture,
        StatePowerDown,
        StateMinimized
    };

    Q_PROPERTY(CameraUnit cameraUnit READ cameraUnit NOTIFY cameraUnitChanged);
    CameraUnit cameraUnit() const { return (CameraUnit)mUnit; }

    Q_PROPERTY(VfMode vfMode READ vfMode NOTIFY vfModeChanged);
    VfMode vfMode() const { return mVfMode; }

    Q_PROPERTY(bool hasFrontCamera READ hasFrontCamera NOTIFY hasFrontCameraChanged)
    bool hasFrontCamera() const { return mHasFrontCamera; }

    Q_PROPERTY(bool hasRearCamera READ hasRearCamera NOTIFY hasRearCameraChanged)
    bool hasRearCamera() const { return mHasRearCamera; }

    Q_PROPERTY(bool canDoVideo READ canDoVideo NOTIFY canDoVideoChanged)
    bool canDoVideo() const { return mCanDoVideo; }

    Q_PROPERTY(bool canCapture READ canCapture NOTIFY canCaptureChanged)
    bool canCapture() const { return mCanCapture; }

    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    bool capturing() const { return mCapturing; }

    Q_INVOKABLE
    int setCameraUnit(CameraUnit unit);

    Q_INVOKABLE
    int setVfMode(VfMode mode);

    Q_INVOKABLE
    void setPreferredCameraSize(QString size);

    Q_INVOKABLE
    int windowAttached();

    Q_INVOKABLE
    int capture();

    Q_INVOKABLE
    int switchCaptureCamera();

    Q_INVOKABLE
    void debugVfSize();

    Q_PROPERTY(int videoBitrate READ videoBitrate NOTIFY videoBitrateChanged)
    int videoBitrate() { return mVideoBitrate; }
    Q_INVOKABLE
    void setVideoBitrate(const int videoBitrate) { mVideoBitrate = videoBitrate; emit videoBitrateChanged(); }

    Q_PROPERTY(double videoFramerate READ videoFramerate NOTIFY videoFramerateChanged)
    double videoFramerate() { return mVideoFramerate; }
    Q_INVOKABLE
    void setVideoFramerate(const double videoFramerate) { mVideoFramerate = videoFramerate; emit videoFramerateChanged(); }

    Q_PROPERTY(int outputWidthDisplay READ outputWidthDisplay NOTIFY outputWidthChanged)
    int outputWidthDisplay();
    Q_PROPERTY(int outputWidth READ outputWidth NOTIFY outputWidthChanged)
    int outputWidth();
    Q_PROPERTY(int outputHeightDisplay READ outputHeightDisplay NOTIFY outputHeightChanged)
    int outputHeightDisplay();
    Q_PROPERTY(int outputHeight READ outputHeight NOTIFY outputHeightChanged)
    int outputHeight();

    Q_PROPERTY(bb::cascades::ArrayDataModel* cameraResolutions READ cameraResolutions NOTIFY cameraResolutionsChanged)
    bb::cascades::ArrayDataModel* cameraResolutions() { return mCameraResolutionsModel; }

    Q_INVOKABLE
    void restartCamera();

    Controller* controller() { return mController; }

signals:
    void cameraUnitChanged(CameraUnit);
    void vfModeChanged(VfMode);
    void hasFrontCameraChanged(bool);
    void hasRearCameraChanged(bool);
    void canDoVideoChanged(bool);
    void canCaptureChanged(bool);
    void suppressStartChanged(bool);
    void capturingChanged(bool);
    void streamingStart();
    void streamingStop();
    void outputWidthChanged();
    void outputHeightChanged();
    void cameraResolutionsChanged();
    void videoBitrateChanged();
    void videoFramerateChanged();

    void requestCameraSwitchabilityToggle(bool value);
    void streamingError(QString error);

public slots:
    void onSystemLanguageChanged();
    void onStatusChanged(camera_devstatus_t status, uint16_t extra);
    void onFullscreen();
    void onThumbnail();
    void onInvisible();
    void onDisplayDirectionChanging(bb::cascades::DisplayDirection::Type displayDirection,
                                    bb::cascades::UIOrientation::Type orientation);
    void onOrientationReadingChanged();
    void onVfParentLayoutFrameChanged(QRectF frame);

private slots:
    void onSwitchCameraTimerTimeout();
    int startVideoVf();
    int stopVideoVf();
    void on_mController_publishError(const QString);

private:
    int runStateMachine(CamState newState);
    int enterState(CamState state, CamState &nextState);
    int exitState();
    void inventoryCameras();
    int openCamera(camera_unit_t unit);
    int closeCamera();
    int startRecording(bool isStartStreaming);
    int stopRecording(bool isStopStreaming);
    int orientationToAngle(bb::cascades::UIOrientation::Type orientation);
    void resourceWarning();
    void poweringDown();
    void updateAngles();
    void updateVideoAngle();
    // using QOrientationSensor in order to know which way is up
    int startOrientationReadings();
    void stopOrientationReadings();
    void constrainViewfinderAspectRatio();
    int discoverCameraCapabilities();
    int discoverVideoCapabilities();
    int discoverVideoVfCapabilities();
    camera_res_t* matchAspectRatio(camera_res_t* target, camera_res_t* resList, int numRes, float accuracy);
    QSize getCameraSizeFromPreferredSize(QList<QSize>sizes, QSize preferredSize);

    const char* stateName(CamState state);

    bool getStoredSupportedResolutions(camera_unit_t unit);
    int retrieveSupportedResolutions(camera_unit_t unit);
    void storeSupportedResolutions(camera_unit_t unit, QList<QSize>sizes);
    void prepareCameraResolutionsModel(camera_unit_t unit);
    void saveSettingsData(const QString key, const QVariant value);

    QTranslator* m_pTranslator;
    bb::cascades::LocaleHandler* m_pLocaleHandler;
    bb::cascades::AbstractPane* mRoot;
    bb::cascades::ForeignWindowControl* mFwc;
    camera_unit_t mUnit;
    camera_handle_t mHandle;
    VfMode mVfMode;
    VfMode mResumeVfMode;
    bool mHasFrontCamera;
    bool mHasRearCamera;
    bool mCanDoVideo;
    bool mCanCapture;
    bool mCapturing;
    bool mSwitchingCamera;
    CamState mState;
    bool mStopViewfinder;
    bool mDeferredResourceWarning;
    StatusThread* mStatusThread;
    bb::cascades::Application* mApp;
    // here is a bunch of rotation state...
    // note that we are tracking display direction via naviagtor signals in order to know which way the UI is being rendered.
    // this will help us to rotate the viewfinder correctly for display.
    // but we are also tracking the device orientation directly via QOrientationSensor.  this is because navigator may not report
    // all orientations (eg. Q10 is not rotatable, and Z10 cannot be used upside-down), but we still want to know which way is up in
    // order to have our videos displayed upright later.
    uint32_t mDisplayDirection;  // as reported by navigator (opposite from the angle we use internally)
    uint32_t mOrientationDirection; // as reported by QOrientationSensor reading (opposite from the angle we use internally)
    uint32_t mDeviceOrientation; // the clockwise-angle equivalent of mOrientationDirection
    uint32_t mDesiredVfAngle;    // the angle we would prefer all viewfinder buffers to be rendered on screen so that pixel 0 is top-left
    uint32_t mDesiredCapAngle;   // the angle we would prefer all captured buffers to be rotated so that pixel 0 is top-left
    uint32_t mVfAngle;           // the angle programmed for viewfinder buffer rotation (desired angle may not be supported)
    uint32_t mCaptureAngle;      // the angle programmed for video capture buffer rotation (desired angle may not be supported)
    uint32_t mWindowAngle;       // the angle to rotate the viewfinder screen window so that the contents appear upright on the display
    bool mRequireUprightVf; // whether vf buffer rotation should be applied, or whether screen rotation is good enough
    bool mRequireUprightCapture; // whether capture buffer rotation should be applied, or whether metadata is good enough
    QtMobility::QOrientationSensor* mOrientationSensor;
    QRectF mVfContainerSize;
    uint32_t mVfWidth;
    uint32_t mVfHeight;
    uint32_t mCapWidth;
    uint32_t mCapHeight;
    int mVideoFileDescriptor;
    // frame formats used by viewfinder & capture
    camera_frametype_t mVideoFormat;
    camera_frametype_t mVideoVfFormat;
    // rotation capabilities
    int mNumVideoRotations;
    uint32_t* mVideoRotations;
    int mNumVideoVfRotations;
    uint32_t* mVideoVfRotations;
    // resolution capabilities
    unsigned int mNumVideoResolutions;
    camera_res_t* mVideoResolutions;
    unsigned int mNumVideoVfResolutions;
    camera_res_t* mVideoVfResolutions;
    QList<QSize>mNumVideoResolutionsList;
    QSize mPreferredCameraSize;
    QList<QSize>mFrontCamVideoResolutions;
    QList<QSize>mRearCamVideoResolutions;
    bb::cascades::ArrayDataModel* mCameraResolutionsModel;
    int mVideoBitrate;
    double mVideoFramerate;

    Controller* mController;
    QTimer* mSwitchCameraTimer;
};

#endif /* __StreamCam_HPP__ */
