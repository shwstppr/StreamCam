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
import com.streamcam.streamcontroller 1.0

StreamCamSheet {
    id: sheet

    onOpened: {
        console.log(streamController.displayServer, Cam.outputWidthDisplay, Cam.outputHeightDisplay)
    }

    Page {
        id: page
        function numericOnly (textin) {                    
            var m_strOut = new String (textin);
            m_strOut = m_strOut.replace(/[^\d.]/g,'');                    
            return m_strOut;
        
        } // end numericOnly
        titleBar: TitleBar {
            title: qsTr("Settings")
            acceptAction: ActionItem {
                title: qsTr("Ok")
                onTriggered: {
                    if(streamController.serverDisplay!=serverInput.text) {
                        streamController.setServer(serverInput.text)
                    }
                    var doRestart = false;
                    if(videoResolutionInput.selectedValue!=qsTr("%1x%2").arg(Cam.outputWidth).arg(Cam.outputHeight)) {
                        if(videoResolutionInput.selectedOption.description.length!=0)
                            mainWindow.toggleCameraSwitchability(false)
                        else
                            mainWindow.toggleCameraSwitchability(true)
                        Cam.setPreferredCameraSize(videoResolutionInput.selectedValue)
                    }
                    var fps = videoFramerateInput.text;
                    if(fps>=15.0 && fps<=30.0 && fps!=Cam.videoFramerate) {
                        Cam.setVideoFramerate(fps);
                        doRestart = true;
                    }
                    var vBitrate = videoBitrateInput.text;
                    if(vBitrate>=128 && fps<=3600 && fps!=Cam.videoBitrate) {
                        Cam.setVideoBitrate(vBitrate);
                        doRestart = true;
                    }
                    if(doRestart)                        
                        Cam.restartCamera();
                    sheet.close()
                }
            }
            dismissAction: ActionItem {
                title: qsTr("Cancel")
                onTriggered: {
                    sheet.close()
                }
            }
        }
        ScrollView {
            scrollViewProperties.scrollMode: ScrollMode.Vertical
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            content: Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: StackLayout {}
                topPadding: ui.sdu(1.0)
                leftPadding: ui.sdu(1.0)
                rightPadding: leftPadding
                bottomPadding: ui.sdu(0.5)
                
                SmallHeadingLabel {
                    text: qsTr("Server")
                }
                TextField {
                    id: serverInput
                    hintText: qsTr("Enter server for broadcast")
                    text: streamController.serverDisplay
                }
                Header {
                    title: qsTr("Video settings")
                }
                SmallHeadingLabel {
                    text: qsTr("Resolution")
                }
                DropDown {
                    id: videoResolutionInput
                    title: "Camera resolution"
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    SmallHeadingLabel {
                        text: qsTr("Bitrate")
                    }
                    Label {
                        textStyle.fontSize: FontSize.XXSmall
                        textStyle.color: Color.Gray
                        verticalAlignment: VerticalAlignment.Bottom
                        text: qsTr("In kbps. Minimum: 128, maximum: 36000")
                    }
                }
                TextField {
                    id: videoBitrateInput
                    inputMode: TextFieldInputMode.NumbersAndPunctuation
                    text: Cam.videoBitrate
                    onTextChanging: {
                        videoBitrateInput.text = page.numericOnly(text);
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    SmallHeadingLabel {
                        text: qsTr("Framerate")
                    }
                    Label {
                        textStyle.fontSize: FontSize.XXSmall
                        textStyle.color: Color.Gray
                        verticalAlignment: VerticalAlignment.Bottom
                        text: qsTr("Minimum: 15.0, maximum: 30.0")
                    }
                }
                TextField {
                    id: videoFramerateInput
                    inputMode: TextFieldInputMode.NumbersAndPunctuation
                    text: Cam.videoFramerate
                    onTextChanging: {
                        videoFramerateInput.text = page.numericOnly(text);
                    }
                }
            }
        }
        attachedObjects: [
            ComponentDefinition {
                id: optionDefinition
                Option {
                    value: text
                }
            }
        ]
        onCreationCompleted: {
            for (var i = 0; i < Cam.cameraResolutions.size(); ++i) {
                var option = optionDefinition.createObject();
                option.text = Cam.cameraResolutions.value(i).text;
                if(option.text == qsTr("%1x%2").arg(Cam.outputWidth).arg(Cam.outputHeight))
                    option.selected = true;
                option.description = Cam.cameraResolutions.value(i).description;
                videoResolutionInput.add(option);
            }
        }
    }
}