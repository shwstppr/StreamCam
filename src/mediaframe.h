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

#ifndef MEDIAFRAME_H_
#define MEDIAFRAME_H_

struct MediaFrame {
    enum MediaFrameType {
        AUDIO = Qt::UserRole+1,
        VIDEO,
        EOS
    };

    QByteArray buffer;
    long dts;
    long pts;
    MediaFrameType type;

    bool isEmpty() {
        return buffer.isEmpty();
    }
};

#endif /* MEDIAFRAME_H_ */
