Wave CloudXR Sample Client

Demonstrate how to program with NVIDIA CloudXR SDK 3.0 for VIVE Focus 3 headset. You can start to develop your own CloudXR application for VIVE Focus 3 based on this sample client.

Requirement
* HTC VIVE Focus 3
* Android development environment
**Android Studio 4.0 or later.
**Android SDK 7.1.1 ‘Nougat’ (API level 25) or higher.
**Android build tools 28.0.3
**Android NDK 21.4.7075529
**OpenJDK 1.8n
* [Google OBOE SDK 1.5.0] (https://github.com/google/oboe/releases/tag/1.5.0)
* [Wave Native SDK 4.1.1] (https://developer.vive.com/resources/vive-wave/download/)

How to build
1. Download CloudXR SDK 3.0 and oboe-1.5.0.aar.
2. Put CloudXR.aar and oboe-1.5.0.aar in [ProjectRoot]/app/libs
3. Download Wave SDK and put in [ProjectRoot]/repo (paths can be modified in build_sdk.gradle)
4. You are ready to build.

How To Use
1. Install CloudXR server 3.0 on your PC.
2. Build Wave CloudXR Sample Client and install the apk to your VIVE Focus 3
3. Modify the IP address in CloudXRLaunchOptions.txt and push it into /sdcard of your VIVE Focus 3. Please read [CloudXR Command-Line Options](https://docs.nvidia.com/cloudxr-sdk/usr_guide/cmd_line_options.html#command-line-options) for the format of this config file, CloudXRLaunchOptions.txt)
4. Launch the apk to start streaming

Note
* The application requires WRITE_EXTERNAL_STORAGE permission to proceed, for loading a config file from sdcard and writing CloudXR logs.
* If RECORD_AUDIO permission is denied, microphone feature will be disabled.


