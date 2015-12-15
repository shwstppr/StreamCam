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

import bb.cascades 1.4
import bb.system 1.0
import com.streamcam.cam 1.0
import com.streamcam.streamcontroller 1.0
import com.streamcam.timer 1.0

NavigationPane {
    id: mainWindow        
    
    function destroySheet(iSheet) {
        sheetDestroyer.currentSheet = iSheet;
        sheetDestroyer.start();
    }
    
    function toggleCameraSwitchability(value) {
        switchCamera.isAllowed = value;
    }
    
    function onStreamingError(error) {
        stopStreamingTimer();
        dialog.cancelButton.label = undefined; //Hack to remove 'Cancel' button from dialog
        dialog.title = qsTr("Streaming error");
        dialog.body = qsTr(error);
        dialog.confirmButton.label = qsTr("Ok");
        dialog.show();
    }
    
    function startStreamingTimer() {
        streamingTimer.elapsed = 0;
        streamingTimer.start();
    }
    
    function stopStreamingTimer() {
        streamingTimer.stop();
        durationLabel.text = "";
    }
    
    Page {
        actionBarVisibility: ChromeVisibility.Compact
        content: Container {
            background: Color.Black        
            layout: DockLayout {}
            
            // the viewfinder window
            ForeignWindowControl {
                id: vfForeignWindow
                // when a window with this id joins the window group, it will be attached to this control
                windowId: "vfWindowId"
                // give the foreign window an objectName so we can look it up from C++
                objectName: "vfForeignWindow"
                // the cascades FWC will update the screen window size, position, and visibility  
                updatedProperties: WindowProperty.Size | WindowProperty.Position | WindowProperty.Visible
                // only becomes visible when the camera viewfinder window is attached
                visible: boundToWindow
                // center the window in the parent container.  we will be adjusting the size to maintain aspect
                // ratio, so let's keep it symmetrical.
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                onWindowAttached: {
                    Cam.windowAttached();
                }
                onWindowDetached: {
                }
            }
            Container {
                id: metaInfoContainer
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Top
                layout: DockLayout {}
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    background: Color.Black
                    opacity: 0.6
                }
                Container {
                    topPadding: 5
                    bottomPadding: topPadding
                    leftPadding: 5
                    rightPadding: leftPadding
                    
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    layout: StackLayout {}
                    Label {
                        id: durationLabel
                        textStyle.color: Color.Green
                        textStyle.fontWeight: FontWeight.Bold
                        visible: text!=""
                    }
                    Container {
                        Label {
                            text: streamController.serverDisplay==""?qsTr("Server not set"):
                            qsTr("Server: %1").arg(streamController.serverDisplay)
                            multiline: true
                            textStyle.color: streamController.serverDisplay==""?Color.Red:Color.White
                            textStyle.fontSize: FontSize.XXSmall
                        }
                    }
                    Container {
                        Label {
                            text: qsTr("Video: H264, %1x%2, %3fps @ %4kbps")
                            .arg(Cam.outputWidthDisplay)
                            .arg(Cam.outputHeightDisplay)
                            .arg(Cam.videoFramerate)
                            .arg(Cam.videoBitrate)
                            visible: streamController.isStreaming
                            textStyle.color: Color.White
                            textStyle.fontSize: FontSize.XXSmall
                        }
                    }
                    Container {
                        Label {
                            text: {
                                var txt = "Audio: AAC";
                                if(streamController.audioChannel)
                                    txt += ", "+streamController.audioChannel;
                                if(streamController.audioSamplingRate)
                                    txt += ", "+streamController.audioSamplingRate;
                                if(streamController.audioBitrate)
                                    txt += " @ "+streamController.audioBitrate;
                                return txt;
                            }
                            visible: streamController.isStreaming
                            textStyle.color: Color.White
                            textStyle.fontSize: FontSize.XXSmall
                        }
                    }
                    Container {
                        Label {
                            text: qsTr("Dropped frames: %1 (%2%)").arg(streamController.droppedFramesCount)
                            .arg(Math.round(streamController.droppedFramesCount*100/streamController.totalFramesCount))
                            visible: streamController.droppedFramesCount>0
                            textStyle.color: Color.White
                            textStyle.fontSize: FontSize.XXSmall
                        }
                    }
                }
            }
        }
        
        actions: [
            ActionItem {
                id: switchCamera
                objectName: "switchCameraButton"
                title: "Switch Camera"
                property bool isAllowed: true;
                imageSource: "asset:///icons/ic_switch_camera.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                enabled: isAllowed && Cam.hasRearCamera && Cam.hasFrontCamera
                onTriggered: {
                    if(Cam.capturing)
                        Cam.switchCaptureCamera();
                    else {
                        if(Cam.cameraUnit == StreamCam.UnitRear)
                            Cam.setCameraUnit(StreamCam.UnitFront);
                        else
                            Cam.setCameraUnit(StreamCam.UnitRear);
                    }
                }
            },
            ActionItem {
                title: metaInfoContainer.visible?qsTr("Hide info"):qsTr("Show info")
                ActionBar.placement: ActionBarPlacement.InOverflow
                imageSource: metaInfoContainer.visible?"asset:///icons/hide.png":"asset:///icons/show.png"
                onTriggered: {
                    metaInfoContainer.visible = ! metaInfoContainer.visible
                }
            },
            ActionItem {
                id: capture
                objectName: "captureButton"
                property bool streamSignalsConnected: false
                // TODO: change text?
                title: Cam.capturing?qsTr("Stop streaming"):qsTr("Start streaming")
                imageSource: Cam.capturing?"asset:///icons/ic_stop.png":"asset:///icons/ic_rec_red.png"
                ActionBar.placement: ActionBarPlacement.Signature
                enabled: Cam.canCapture
                attachedObjects: [
                    SystemToast {
                        id: serverNotSetToast
                        body: "Please set streaming server first!"
                        button.label: "Ok"
                        button.enabled: true
                        onFinished: {
                            if(result==SystemUiResult.ButtonSelection) {                                
                                var sheet = settingsSheetDefinition.createObject(mainWindow);
                                sheet.open();
                            }                                
                        }
                    }   
                ]
                onTriggered: {
                    if(!streamSignalsConnected) {
                        Cam.streamingError.connect(mainWindow.onStreamingError);
                        Cam.streamingStart.connect(mainWindow.startStreamingTimer);
                        Cam.streamingStop.connect(mainWindow.stopStreamingTimer);
                        streamSignalsConnected = true;
                    }
                    if(streamController.serverDisplay=="") {
                        serverNotSetToast.show();
                        return;
                    }
                    Cam.capture();
                }
            },
            ActionItem {
                title: qsTr("Stream Settings")
                ActionBar.placement: ActionBarPlacement.InOverflow
                enabled: !Cam.capturing
                onTriggered: {
                    var sheet = settingsSheetDefinition.createObject(mainWindow);
                    sheet.open();
                }
            }
        ]
    }
    Menu.definition: MenuDefinition {       
        actions: [
            ActionItem {
                title: qsTr("About")
                enabled: !streamController.isStreaming
                imageSource: "asset:///icons/info.png"
                onTriggered: {
                    var npage = aboutPageDefinition.createObject();
                    mainWindow.push(npage);
                }
                attachedObjects: [
                    ComponentDefinition {
                        id: aboutPageDefinition
                        source: "AboutPage.qml"
                    }
                ]
            }
        ]
    }
    attachedObjects: [
        ComponentDefinition {
            id: settingsSheetDefinition
            source: "asset:///SettingsSheet.qml"
        },
        Timer {
            id: sheetDestroyer
            property variant currentSheet: 0
            interval: 500
            singleShot: true
            onTimeout: {
                if(currentSheet==0) return;
                currentSheet.destroy();
                currentSheet = 0;
            }
        },
        Timer {
            id: streamingTimer
            interval: 1000
            property int elapsed: 0
            onTimeout: {
                elapsed++;
                var intHours = Math.floor(elapsed/3600);
                var stringHours = "0";
                if(intHours<10)
                    stringHours+=""+intHours;
                else 
                    stringHours = ""+intHours;
                var intMinutes = Math.floor(Math.floor(elapsed/60)%60);
                var stringMinutes = "0";
                if(intMinutes<10)
                    stringMinutes+=""+intMinutes;
                else 
                    stringMinutes = ""+intMinutes;
                var intSeconds = Math.floor(elapsed%60);
                var stringSeconds = "0";
                if(intSeconds<10)
                    stringSeconds+=""+intSeconds;
                else 
                    stringSeconds = ""+intSeconds;
                durationLabel.text = qsTr("%1:%2:%3").arg(stringHours).arg(stringMinutes).arg(stringSeconds);
            }
        },
        SystemDialog {
            id: dialog
        }
    ]
    onCreationCompleted: {
        Cam.requestCameraSwitchabilityToggle.connect(mainWindow.toggleCameraSwitchability);
    }
}
