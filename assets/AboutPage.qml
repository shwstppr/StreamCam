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

import bb.cascades 1.3

Page {
    id: page
    titleBar: TitleBar {
        title: qsTr("About")
    }
    content: ScrollView {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        scrollViewProperties.scrollMode: ScrollMode.Vertical
        content: Container {
            leftPadding: ui.sdu(2)
            rightPadding: leftPadding
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            layout: StackLayout {}
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Container {
                    topPadding: ui.sdu(4)
                    layout: StackLayout {
                    }
                    ImageView {
                        imageSource: "asset:///icons/icon.png"
                        maxWidth: 128
                        maxHeight: 128
                    }
                }
                Container {
                    leftPadding: ui.sdu(3)
                    layout: StackLayout {
                    }
                    Label {
                        text: qsTr("StreamCam")
                        textStyle.base: SystemDefaults.TextStyles.BigText
                    }
                    Label {
                        multiline: true
                        textFormat: TextFormat.Html
                        text: qsTr("Developed by Abhishek Kumar.<br/>Copyright© 2015 Abhishek Kumar.")
                    }
                }
            }
            Label {
                multiline: true
                textFormat: TextFormat.Html
                text: qsTr("StreamCam allows user to stream BlackBerry 10 device camera to a nginx-RTMP server.<br/>"+
                "Guide for setting up personal nginX-RTMP server can be found here, "+
                "<a href='https://obsproject.com/forum/resources/how-to-set-up-your-own-private-rtmp-server-using-nginx.50/'>How to set up your own private RTMP server using nginx</a>.<br/>"+
                "For Windows check <a href='https://github.com/illuspas/nginx-rtmp-win32'>illuspas/nginx-rtmp-win32</a>.<br/>"+
                "More info about nginX, <a href='https://www.nginx.com/'>www.nginx.com</a>.<br/><br/>"+
                "Unfortunately StreamCam doesn't support streaming to any Flash RTMP server as Flash streaming would have required 44.1kHz audio and BlackBerry 10 devices' camera give only 48 kHz audio.<br/>"+
                "Also, writing code for muxing for Flash streaming was a little difficult in any case.<br/>"+
                "And, StreamCam can't store streamed video as there is no in-built muxing class in BlackBerry 10 development framwork and it is not possible for me to write my own at present.<br/><br/>"+
                "Send your queries to <a href='mailto:abhishek.mrt22@gmail.com'>abhishek.mrt22@gmail.com</a>.<br/><br/>"+
                "Source for StreamCam can be found somewhere here, <a href='https://github.com/sh0wstopper'>Github - sh0wstopper</a>. ")
            } // Label
        }
    }
}
