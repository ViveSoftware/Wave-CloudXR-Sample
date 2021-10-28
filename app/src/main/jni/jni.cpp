//========= Copyright 2016-2021, HTC Corporation. All rights reserved. ===========

#include <jni.h>
#include <log.h>
#include <WaveCloudXRApp.h>
#include <unistd.h>
#include <wvr/wvr.h>

bool gPaused = false;

int main(int argc, char *argv[]) {

    WaveCloudXRApp *app = new WaveCloudXRApp();
    if (!app->initVR()) {
        app->shutdownVR();
        delete app;
        return 1;
    }

    if (!app->initGL()) {
        app->shutdownGL();
        app->shutdownVR();
        delete app;
        return 1;
    }

    while (1) {
        if (app->initCloudXR()) {
            app->HandleCloudXRLifecycle(gPaused);
        }

        if (app->handleInput())
            break;

        if (app->renderFrame()) {
            LOGE("Stop streaming.");
            break;
        }

        app->updatePose();
    }

    app->shutdownGL();
    app->shutdownVR();
    app->shutdownCloudXR();

    delete app;
    return 0;
}

extern "C" JNIEXPORT void JNICALL Java_com_htc_vr_samples_wavecloudxr_MainActivity_nativeInit(JNIEnv * env, jobject activityInstance, jobject assetManagerInstance) {
    LOGI("register WVR main");
    WVR_RegisterMain(main);
}

extern "C" JNIEXPORT void JNICALL Java_com_htc_vr_samples_wavecloudxr_MainActivity_nativeOnPause(JNIEnv * env, jobject activityInstance) {
    gPaused = true;
}

extern "C" JNIEXPORT void JNICALL Java_com_htc_vr_samples_wavecloudxr_MainActivity_nativeOnResume(JNIEnv * env, jobject activityInstance) {
    gPaused = false;
}


jint JNI_OnLoad(JavaVM* vm, void* reserved) {

    return JNI_VERSION_1_6;
}

jint JNI_OnUnLoad(JavaVM* vm, void* reserved) {

    return 0;
}

