# StreamCam
BlackBerry 10 app for livestreaming to nginX-RTMP server.

Guide for setting up personal nginX-RTMP server can be found here, [How to set up your own private RTMP server using nginx](https://obsproject.com/forum/resources/how-to-set-up-your-own-private-rtmp-server-using-nginx.50)

For running nginX server on Windows with RTMP module, check https://github.com/illuspas/nginx-rtmp-win32

Unfortunately, StreamCam doesn't support streaming to any Flash RTMP server as Flash streaming would have required 44.1kHz audio and BlackBerry 10 devices' camera give only 48 kHz audio.
Also, StreamCam can not store streamed video as there is no in-built muxing class in BlackBerry 10 development framwork and author is not smart enough to write one of his own.

Most of the code for handling camera is taken/inspired from one of the BlackBerry 10 Cascades Community Sample, [BestCamera](https://github.com/blackberry/Cascades-Community-Samples/tree/master/BestCamera).
