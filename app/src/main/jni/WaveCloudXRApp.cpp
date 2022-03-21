//========= Copyright 2016-2021, HTC Corporation. All rights reserved. ===========

#include <log.h>
#include <string.h>
#include <sys/time.h>

#include <egl/egl.h>
#include <GLES3/gl31.h>
#include <GLES3/gl3ext.h>
#include <GLES3/gl3.h>

#include <wvr/wvr.h>
#include <wvr/wvr_ctrller_render_model.h>
#include <wvr/wvr_render.h>
#include <wvr/wvr_device.h>
#include <wvr/wvr_projection.h>
#include <wvr/wvr_overlay.h>
#include <wvr/wvr_system.h>
#include <wvr/wvr_events.h>

#include "WaveCloudXRApp.h"

// Return micro second.  Should always positive because now is bigger.
#define timeval_subtract(now, last) \
    ((now.tv_sec - last.tv_sec) * 1000000LL + now.tv_usec - last.tv_usec)

#define VR_MAX_CLOCKS 200
#define LATCHFRAME_TIMEOUT_MS 100 // timeout to fetch a frame from CloudXR server
#define FRAME_TIMEOUT_SECOND 30.0f // exit program if no incoming frame for too long

WaveCloudXRApp::WaveCloudXRApp()
        : mTimeDiff(0.0f)
        , mLeftEyeQ(nullptr)
        , mRightEyeQ(nullptr)
        , mReceiver(nullptr)
        , mPlaybackStream(nullptr)
        , mRecordStream(nullptr)
        , mClientState(cxrClientState_ReadyToConnect)
        , mClientStateReason(cxrStateReason_NoError)
        , mRenderWidth(1720)
        , mRenderHeight(1720)
        , mConnected(false)
        , mInited(false)
        , mPaused(true)
        {}

bool WaveCloudXRApp::initVR() {

    // Init WVR Runtime
    WVR_InitError eError = WVR_Init(WVR_AppType_VRContent);
    if (eError != WVR_InitError_None) {
        LOGE("Unable to init VR runtime: %s", WVR_GetInitErrorString(eError));
        return false;
    }

/*
    Controller mapping
*/
    mIs6DoFHMD = false;
    if (WVR_NumDoF_6DoF==WVR_GetDegreeOfFreedom(WVR_DeviceType_HMD))
        mIs6DoFHMD = true;

    mIs6DoFController[0] = (WVR_GetDegreeOfFreedom(WVR_DeviceType_Controller_Left)==WVR_NumDoF_6DoF);
    mIs6DoFController[1] = (WVR_GetDegreeOfFreedom(WVR_DeviceType_Controller_Right)==WVR_NumDoF_6DoF);
    LOGV("6DOF HMD = %d, Con1 = %d, Con2 = %d.", mIs6DoFHMD?1:0, mIs6DoFController[0]?1:0, mIs6DoFController[1]?1:0);

    WVR_SetPosePredictEnabled(WVR_DeviceType_HMD, true, true);
    WVR_SetPosePredictEnabled(WVR_DeviceType_Controller_Left, true, true);
    WVR_SetPosePredictEnabled(WVR_DeviceType_Controller_Right, true, true);

    WVR_SetArmModel(WVR_SimulationType_Auto);
    WVR_SetArmSticky(false);

    WVR_InputAttribute inputIdAndTypes[] =
    {
            {WVR_InputId_Alias1_System, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Menu, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Grip, WVR_InputType_Button | WVR_InputType_Touch | WVR_InputType_Analog, WVR_AnalogType_1D},
            {WVR_InputId_Alias1_DPad_Left, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_DPad_Up, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_DPad_Right, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_DPad_Down, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Volume_Up, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Volume_Down, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Bumper, WVR_InputType_Button , WVR_AnalogType_None},
            {WVR_InputId_Alias1_A, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_B, WVR_InputType_Button, WVR_AnalogType_None},
            // {WVR_InputId_Alias1_X, WVR_InputType_Button, WVR_AnalogType_None},
            // {WVR_InputId_Alias1_Y, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Back, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Enter, WVR_InputType_Button, WVR_AnalogType_None},
            {WVR_InputId_Alias1_Touchpad, WVR_InputType_Button | WVR_InputType_Touch | WVR_InputType_Analog, WVR_AnalogType_2D},
            {WVR_InputId_Alias1_Thumbstick, WVR_InputType_Button | WVR_InputType_Touch | WVR_InputType_Analog, WVR_AnalogType_2D},
            {WVR_InputId_Alias1_Trigger, WVR_InputType_Button | WVR_InputType_Touch | WVR_InputType_Analog, WVR_AnalogType_1D}
    };

    const size_t numAttribs = sizeof(inputIdAndTypes)/sizeof(*inputIdAndTypes);

    WVR_SetInputRequest(WVR_DeviceType_HMD, inputIdAndTypes, numAttribs);
    WVR_SetInputRequest(WVR_DeviceType_Controller_Left, inputIdAndTypes, numAttribs);
    WVR_SetInputRequest(WVR_DeviceType_Controller_Right, inputIdAndTypes, numAttribs);

    // Init Wave Render
    WVR_RenderInitParams_t param;
    param = { WVR_GraphicsApiType_OpenGL, WVR_RenderConfig_sRGB };

    WVR_RenderError pError = WVR_RenderInit(&param);
    if (pError != WVR_RenderError_None) {
        LOGE("Present init failed - Error[%d]", pError);
    }

    mInteractionMode = WVR_GetInteractionMode();
    mGazeTriggerType = WVR_GetGazeTriggerType();
    LOGI("initVR() mInteractionMode: %d, mGazeTriggerType: %d", mInteractionMode, mGazeTriggerType);
    return true;
}

bool WaveCloudXRApp::initGL() {

    gettimeofday(&mRtcTime, NULL);

    // Setup stereo render targets
    WVR_GetRenderTargetSize(&mRenderWidth, &mRenderHeight);
    LOGD("Recommended size is %ux%u", mRenderWidth, mRenderHeight);
    if (mRenderWidth == 0 || mRenderHeight == 0) {
        LOGE("Please check server configure");
        return false;
    }

    mLeftEyeQ = WVR_ObtainTextureQueue(WVR_TextureTarget_2D, WVR_TextureFormat_RGBA, WVR_TextureType_UnsignedByte, mRenderWidth, mRenderHeight, 0);
    for (int i = 0; i < WVR_GetTextureQueueLength(mLeftEyeQ); i++) {
        GLuint fbo = CreateGLFramebuffer((GLuint)(size_t)WVR_GetTexture(mLeftEyeQ, i).id);
        mLeftEyeFBO.push_back(fbo);
    }

    mRightEyeQ = WVR_ObtainTextureQueue(WVR_TextureTarget_2D, WVR_TextureFormat_RGBA, WVR_TextureType_UnsignedByte, mRenderWidth, mRenderHeight, 0);
    for (int i = 0; i < WVR_GetTextureQueueLength(mRightEyeQ); i++) {
        GLuint fbo = CreateGLFramebuffer((GLuint)(size_t)WVR_GetTexture(mRightEyeQ, i).id);
        mRightEyeFBO.push_back(fbo);
	}

    return true;
}

GLuint WaveCloudXRApp::CreateGLFramebuffer(const GLuint texId)
{
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);

    GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOGE("Incomplete frame buffer object (%d). Requested dimensions: %d x %d.", status, mRenderWidth, mRenderHeight);
    }
    return fbo;
}

void WaveCloudXRApp::shutdownGL() {
    
    ReleaseFramebuffers();
}

void WaveCloudXRApp::shutdownVR() {
    WVR_Quit();
}

void WaveCloudXRApp::shutdownCloudXR() {
    if (mPlaybackStream)
    {
        mPlaybackStream->close();
    }

    if (mRecordStream)
    {
        mRecordStream->close();
    }

    if (mReceiver)
    {
        cxrDestroyReceiver(mReceiver);
        mReceiver = nullptr;
    }
}


void WaveCloudXRApp::HandleCloudXRLifecycle(const bool pause)
{
    if (pause) Pause();
    else Resume();
}


//-----------------------------------------------------------------------------
// Purpose: Poll events.  Quit application if return true.
//-----------------------------------------------------------------------------
bool WaveCloudXRApp::handleInput() {
    // Process WVR events
    WVR_Event_t event;
    while(WVR_PollEventQueue(&event)) {
        if (event.common.type == WVR_EventType_Quit) {
            shutdownCloudXR();
            return true;
        }

        if (event.common.type == WVR_EventType_DeviceConnected) {
            LOGI("WVR_EventType_DeviceConnected");
        }
        if (event.common.type == WVR_EventType_DeviceDisconnected) {
            LOGI(" WVR_EventType_DeviceDisconnected)");
        }

        processVREvent(event);

        // Update input events to streaming server
        UpdateInput(event);
    }
    UpdateAnalog();

    return false;
}

void WaveCloudXRApp::ReleaseFramebuffers()
{
    if (mLeftEyeQ != 0) {
        for (int i = 0; i < WVR_GetTextureQueueLength(mLeftEyeQ); i++) {
            glDeleteFramebuffers(1, &mLeftEyeFBO.at(i));
        }
        WVR_ReleaseTextureQueue(mLeftEyeQ);
    }

    if (mRightEyeQ != 0) {
        for (int i = 0; i < WVR_GetTextureQueueLength(mRightEyeQ); i++) {
            glDeleteFramebuffers(1, &mRightEyeFBO.at(i));
        }
        WVR_ReleaseTextureQueue(mRightEyeQ);
    }
}

void WaveCloudXRApp::RecreateFramebuffer(const uint32_t width, const uint32_t height) {

    if (mRenderWidth == width && mRenderHeight == height)
        return;

    ReleaseFramebuffers();

    mLeftEyeQ = WVR_ObtainTextureQueue(WVR_TextureTarget_2D, WVR_TextureFormat_RGBA, WVR_TextureType_UnsignedByte, width, height, 0);
    for (int i = 0; i < WVR_GetTextureQueueLength(mLeftEyeQ); i++) {
        GLuint fbo = CreateGLFramebuffer((GLuint)(size_t)WVR_GetTexture(mLeftEyeQ, i).id);
        mLeftEyeFBO.push_back(fbo);
    }

    mRightEyeQ = WVR_ObtainTextureQueue(WVR_TextureTarget_2D, WVR_TextureFormat_RGBA, WVR_TextureType_UnsignedByte, width, height, 0);
    for (int i = 0; i < WVR_GetTextureQueueLength(mRightEyeQ); i++) {
        GLuint fbo = CreateGLFramebuffer((GLuint)(size_t)WVR_GetTexture(mRightEyeQ, i).id);
        mRightEyeFBO.push_back(fbo);
    }

    mRenderWidth = width;
    mRenderHeight = height;
    LOGD("Recreated buffer %dx%d", mRenderWidth, mRenderHeight);
}

void WaveCloudXRApp::switchGazeTriggerType() {
    if (mGazeTriggerType == WVR_GazeTriggerType_Timeout) {
        mGazeTriggerType = WVR_GazeTriggerType_Button;
    } else if (mGazeTriggerType == WVR_GazeTriggerType_Button) {
        mGazeTriggerType = WVR_GazeTriggerType_TimeoutButton;
    } else if (mGazeTriggerType == WVR_GazeTriggerType_TimeoutButton) {
        mGazeTriggerType = WVR_GazeTriggerType_Timeout;
    }
    LOGD("switchGazeTriggerType mGazeTriggerType: %d", mGazeTriggerType);
    WVR_SetGazeTriggerType(mGazeTriggerType);
}

//-----------------------------------------------------------------------------
// Purpose: Processes a single VR event
//-----------------------------------------------------------------------------
void WaveCloudXRApp::processVREvent(const WVR_Event_t & event) {
    switch(event.common.type) {
    case WVR_EventType_DeviceConnected:
        {

        }
        break;
    case WVR_EventType_ButtonPressed:
    case WVR_EventType_ButtonUnpressed:
    case WVR_EventType_TouchTapped:
    case WVR_EventType_TouchUntapped:
        {

        }
        break;
    case WVR_EventType_DeviceRoleChanged:
        {

        }
        break;
    case WVR_EventType_DeviceStatusUpdate:
        {
            LOGD("Device %u updated.\n", event.device.deviceType);

        }
        break;
    case WVR_EventType_IpdChanged:
        {
            WVR_RenderProps_t props;
            bool ret = WVR_GetRenderProps(&props);
            float ipd = 0;
            if (ret) {
                ipd = props.ipdMeter;
            }
            LOGI("Receive WVR_EventType_IpdChanged mode = %d", ipd);
        }
        break;
    case WVR_EventType_InteractionModeChanged:
        {
            mInteractionMode = WVR_GetInteractionMode();
            LOGI("Receive WVR_EventType_InteractionModeChanged mode = %d", mInteractionMode);
        }
        break;
    case WVR_EventType_GazeTriggerTypeChanged:
        {
            mGazeTriggerType = WVR_GetGazeTriggerType();
            LOGI("Receive WVR_EventType_GazeTriggerTypeChanged type = %d", mGazeTriggerType);
        }
        break;
    // If WaveVR AQ have enabled with WVR_QualityStrategy_SendQualityEvent, you will receive
    // the wvr recommendedQuality_Lower/Higher event based on rendering performance.
    case WVR_EventType_RecommendedQuality_Lower:
        {
            /* Once you got this recommend event:
             * 1. If your conetent rendering quality has already in the worst quality, you can ignore this event.
             * 2. Or, you can adjust the rendering resolution lower gradually by re-create texture queue, disable MSAA, etc.
             */
            //LOGI("Get WVR_EventType_RecommendedQuality_Lower");
        }
        break;
    case WVR_EventType_RecommendedQuality_Higher:
        {
            /* 1. Once you got this recommend event:
             * 1. If your conetent rendering quality has already in the best quality, you can ignore this event.
             * 2. Or, you can adjust the rendering resolution higher ASAP by re-create texture queue, enable MSAA, etc.
             */
            //LOGI("Get WVR_EventType_RecommendedQuality_Higher");
        }
        break;
    default:
        break;
    }
}

bool WaveCloudXRApp::renderFrame() {
    updateTime();

    static float loadingTime = 0.0f;    
    bool frameValid = UpdateFrame();

    if (!frameValid) {
        // Exit program when no valid frame for too long
        loadingTime += mTimeDiff;
        if (loadingTime > FRAME_TIMEOUT_SECOND) {
            LOGD("No valid frame for %f seconds", loadingTime);
            return true;
        }
    } else {
        loadingTime = 0.0f;
    }

    /*
     * Render & Submit
     * Left Eye
     * */
    int32_t leftIdx = WVR_GetAvailableTextureIndex(mLeftEyeQ);
    GLuint fbo = mLeftEyeFBO.at(leftIdx);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

    WVR_TextureParams_t leftEyeTexture = WVR_GetTexture(mLeftEyeQ, leftIdx);

    glViewport(0, 0, mRenderWidth, mRenderHeight);
    glScissor(0, 0, mRenderWidth, mRenderHeight);
    WVR_PreRenderEye(WVR_Eye_Left, &leftEyeTexture);
    WVR_RenderMask(WVR_Eye_Left);

    glClear(GL_COLOR_BUFFER_BIT);
    Render(WVR_Eye_Left, leftEyeTexture, frameValid);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    /*
     * Render & Submit
     * Right Eye
     * */
    int32_t rightIdx = WVR_GetAvailableTextureIndex(mRightEyeQ);
    fbo = mRightEyeFBO.at(rightIdx);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    WVR_TextureParams_t rightEyeTexture = WVR_GetTexture(mRightEyeQ, rightIdx);

    glViewport(0, 0, mRenderWidth, mRenderHeight);
    glScissor(0, 0, mRenderWidth, mRenderHeight);
    WVR_PreRenderEye(WVR_Eye_Right, &rightEyeTexture);
    WVR_RenderMask(WVR_Eye_Right);

    glClear(GL_COLOR_BUFFER_BIT);
    Render(WVR_Eye_Right, rightEyeTexture, frameValid);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Clear
    {
        // We want to make sure the glFinish waits for the entire present to complete, not just the submission
        // of the command. So, we do a clear here right here so the glFinish will wait fully for the swap.
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    usleep(1); // ?

    return false;
}

void WaveCloudXRApp::updateTime() {
    // Process time variable.
    struct timeval now;
    gettimeofday(&now, NULL);

    mClockCount++;
    if (mRtcTime.tv_usec > now.tv_usec)
        mClockCount = 0;
    if (mClockCount >= VR_MAX_CLOCKS)
        mClockCount--;

    uint32_t timeDiff = timeval_subtract(now, mRtcTime);
    mTimeDiff = timeDiff / 1000000.0f;
    mTimeAccumulator2S += timeDiff;
    mRtcTime = now;
    mFrameCount++;
    if (mTimeAccumulator2S > 2000000) {
        mFPS = mFrameCount / (mTimeAccumulator2S / 1000000.0f);
        LOGI("FPS %2.0f", mFPS);

        mFrameCount = 0;
        mTimeAccumulator2S = 0;
    }
}

void WaveCloudXRApp::updatePose() {

    // Get all device poses at once but it's a blocking call
    // WVR_GetSyncPose(WVR_PoseOriginModel_OriginOnHead, mVRDevicePairs, WVR_DEVICE_COUNT_LEVEL_1);

    // mutex?
    {
        WVR_PoseOriginModel pom = WVR_PoseOriginModel_OriginOnGround;
        // Returns immediately with latest pose
        WVR_GetPoseState(WVR_DeviceType_HMD, pom, 0, &mHmdPose);
        UpdateHMDPose(mHmdPose);

        pom = mIs6DoFHMD ? WVR_PoseOriginModel_OriginOnGround
                         : WVR_PoseOriginModel_OriginOnHead_3DoF,
                WVR_GetPoseState(WVR_DeviceType_Controller_Left, pom, 0, &mCtrlPoses[0]);
        UpdateDevicePose(WVR_DeviceType_Controller_Left, mCtrlPoses[0]);

        WVR_GetPoseState(WVR_DeviceType_Controller_Right, pom, 0, &mCtrlPoses[1]);
        UpdateDevicePose(WVR_DeviceType_Controller_Right, mCtrlPoses[1]);
    }
}

/* CloudXR */

WVR_Matrix4f_t Convert(const cxrMatrix34& mtx)
{
    WVR_Matrix4f_t out{};
    memcpy(&out, &mtx, sizeof(mtx));
    out.m[3][3] = 1.0f;

    return out;
}

cxrMatrix34 Convert(const WVR_Matrix4f_t& mtx)
{
    cxrMatrix34 out{};
    memcpy(&out, &mtx, sizeof(out));

    return out;
}

cxrVector3 Convert(const WVR_Vector3f& vec)
{
    return cxrVector3{vec.v[0], vec.v[1], vec.v[2]};
}

WVR_Vector3f Convert(const cxrVector3& vec)
{
    return WVR_Vector3f{vec.v[0], vec.v[1], vec.v[2]};
}

// Make sure storage permission is granted before LoadConfig() and InitReceiver()
// Make sure WaveVR is initialized before InitDeviceDesc()
bool WaveCloudXRApp::initCloudXR() {

    if (mInited) {
        return true;
    }

    if (!LoadConfig()) return false;
    if (!InitCallbacks()) return false;
    if (!InitDeviceDesc()) return false;
    if (!InitReceiver()) return false;
    if (!InitAudio()) return false;

    mInited = true;
    LOGW("CloudXR initialization success");
    return mInited;
}

bool WaveCloudXRApp::LoadConfig() {

    ParseStatus result = mOptions.ParseFile("/sdcard/CloudXRLaunchOptions.txt");

    bool ret = false;
    switch(result)
    {
        case ParseStatus_Success:
            LOGI("Loaded server IP from config: %s", mOptions.mServerIP.c_str());
            ret = true;
            break;
        case ParseStatus_FileNotFound:
            // LOGE("Config file not found.");
        case ParseStatus_Fail:
        case ParseStatus_ExitRequested:
        case ParseStatus_BadVal:
            // LOGE("Config file loading failed.");
            ret = false;
            break;
        default:
            break;
    }
    return ret;
}

bool WaveCloudXRApp::InitCallbacks() {

    mClientCallbacks.GetTrackingState = [](void* context, cxrVRTrackingState* trackingState)
    {
        return reinterpret_cast<WaveCloudXRApp*>(context)->GetTrackingState(trackingState);
    };
    mClientCallbacks.TriggerHaptic = [](void* context, const cxrHapticFeedback* haptic)
    {
        return reinterpret_cast<WaveCloudXRApp*>(context)->TriggerHaptic(haptic);
    };
    mClientCallbacks.RenderAudio = [](void* context, const cxrAudioFrame *audioFrame)
    {
        return reinterpret_cast<WaveCloudXRApp*>(context)->RenderAudio(audioFrame);
    };
    //mClientCallbacks.ReceiveUserData = [](void* context, const void* data, uint32_t size)
    //{
    //    return reinterpret_cast<CloudXRStream*>(context)->ReceiveUserData(data, size);
    //};

    mClientCallbacks.UpdateClientState = [](void* context, cxrClientState state, cxrStateReason reason)
    {
        return reinterpret_cast<WaveCloudXRApp*>(context)->HandleClientState(state, reason);
    };

    return true;
}

bool WaveCloudXRApp::InitAudio() {

    if (mDeviceDesc.receiveAudio)
    {
        // Initialize audio playback
        oboe::AudioStreamBuilder playbackStreamBuilder;
        playbackStreamBuilder.setDirection(oboe::Direction::Output);
        playbackStreamBuilder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        playbackStreamBuilder.setSharingMode(oboe::SharingMode::Exclusive);
        playbackStreamBuilder.setFormat(oboe::AudioFormat::I16);
        playbackStreamBuilder.setChannelCount(oboe::ChannelCount::Stereo);
        playbackStreamBuilder.setSampleRate(CXR_AUDIO_SAMPLING_RATE);

        // TODO: proceed without audio?
        oboe::Result r = playbackStreamBuilder.openStream(&mPlaybackStream);
        if (r != oboe::Result::OK) {
            LOGE("Failed to open playback stream. Error: %s", oboe::convertToText(r));
            return false;
        }

        int bufferSizeFrames = mPlaybackStream->getFramesPerBurst() * 2;
        r = mPlaybackStream->setBufferSizeInFrames(bufferSizeFrames);
        if (r != oboe::Result::OK) {
            LOGE("Failed to set playback stream buffer size to: %d. Error: %s",
                 bufferSizeFrames, oboe::convertToText(r));
            return false;
        }

        r = mPlaybackStream->start();
        if (r != oboe::Result::OK) {
            LOGE("Failed to start playback stream. Error: %s", oboe::convertToText(r));
            return false;
        }
    }

    if (mDeviceDesc.sendAudio)
    {
        // Initialize audio recording
        oboe::AudioStreamBuilder recordingStreamBuilder;
        recordingStreamBuilder.setDirection(oboe::Direction::Input);
        recordingStreamBuilder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        recordingStreamBuilder.setSharingMode(oboe::SharingMode::Exclusive);
        recordingStreamBuilder.setFormat(oboe::AudioFormat::I16);
        recordingStreamBuilder.setChannelCount(oboe::ChannelCount::Stereo);
        recordingStreamBuilder.setSampleRate(CXR_AUDIO_SAMPLING_RATE);
        recordingStreamBuilder.setInputPreset(oboe::InputPreset::VoiceCommunication);
        recordingStreamBuilder.setDataCallback(this);

        oboe::Result r = recordingStreamBuilder.openStream(&mRecordStream);
        if (r != oboe::Result::OK) {
            LOGE("Failed to open recording stream. Error: %s", oboe::convertToText(r));
            LOGE("Continuing to run, without recording ability.");
            mDeviceDesc.sendAudio = false;
        } else {
            r = mRecordStream->start();
            if (r != oboe::Result::OK)
            {
                LOGE("Failed to start recording stream. Error: %s", oboe::convertToText(r));
                LOGE("Continuing to run, without recording ability.");
                mRecordStream->close();
                mDeviceDesc.sendAudio = false;
            }
        }
    }

    return true;
}

bool WaveCloudXRApp::InitDeviceDesc() {
    WVR_RenderProps_t props;
    WVR_GetRenderProps(&props);

    mDeviceDesc.deliveryType = cxrDeliveryType_Stereo_RGB;

    mDeviceDesc.maxResFactor = mOptions.mMaxResFactor;

    // checkme: these are not getting updated after resizing
    mDeviceDesc.height = mRenderHeight;
    mDeviceDesc.width = mRenderWidth;

    mDeviceDesc.fps = props.refreshRate;
    mDeviceDesc.ipd = props.ipdMeter;
    mDeviceDesc.receiveAudio = mOptions.mReceiveAudio;
    mDeviceDesc.sendAudio = mOptions.mSendAudio;
    mDeviceDesc.posePollFreq = 0;

    mDeviceDesc.ctrlType = cxrControllerType_OculusTouch;
    mDeviceDesc.disablePosePrediction = false;
    mDeviceDesc.angularVelocityInDeviceSpace = false;
    mDeviceDesc.foveatedScaleFactor = (mOptions.mFoveation < 100) ? mOptions.mFoveation : 0;

    // Frustum
    float l,r,t,b;
    for (int i=0; i<2; ++i) {
        WVR_GetClippingPlaneBoundary((WVR_Eye)i, &l, &r, &t, &b);
        if(l < 0) l *= -1;
        if(r < 0) r *= -1;
        if(t < 0) t *= -1;
        if(b < 0) b *= -1;
        mDeviceDesc.proj[i][0] = -l;
        mDeviceDesc.proj[i][1] = r;
        mDeviceDesc.proj[i][2] = -b;
        mDeviceDesc.proj[i][3] = t;
    }

    mDeviceDesc.predOffset = 0.01f;

    // Set up server chaperone play area
    mDeviceDesc.chaperone.universe = cxrUniverseOrigin_Standing;
    mDeviceDesc.chaperone.origin.m[0][0] = mDeviceDesc.chaperone.origin.m[1][1] = mDeviceDesc.chaperone.origin.m[2][2] = 1;
    mDeviceDesc.chaperone.origin.m[0][1] = mDeviceDesc.chaperone.origin.m[0][2] = mDeviceDesc.chaperone.origin.m[0][3] = 0;
    mDeviceDesc.chaperone.origin.m[1][0] = mDeviceDesc.chaperone.origin.m[1][2] = mDeviceDesc.chaperone.origin.m[1][3] = 0;
    mDeviceDesc.chaperone.origin.m[2][0] = mDeviceDesc.chaperone.origin.m[2][1] = mDeviceDesc.chaperone.origin.m[2][3] = 0;

    WVR_Arena_t arena = WVR_GetArena();
    if (arena.shape == WVR_ArenaShape_Round)
    {
        mDeviceDesc.chaperone.playArea.v[0] = arena.area.round.diameter;
        mDeviceDesc.chaperone.playArea.v[1] = arena.area.round.diameter;
    } else if (arena.shape == WVR_ArenaShape_Rectangle ) {
        mDeviceDesc.chaperone.playArea.v[0] = arena.area.rectangle.width;
        mDeviceDesc.chaperone.playArea.v[1] = arena.area.rectangle.length;
    } else {
        mDeviceDesc.chaperone.playArea.v[0] = 1.0f;
        mDeviceDesc.chaperone.playArea.v[1] = 1.0f;
    }

    LOGI("Device property: IPD: %f, FPS: %f, display %dx%d, play area %.2fx%.2f\n",
         mDeviceDesc.ipd, mDeviceDesc.fps,
         mDeviceDesc.width, mDeviceDesc.height,
         mDeviceDesc.chaperone.playArea.v[0], mDeviceDesc.chaperone.playArea.v[1]);

    return true;
}

bool WaveCloudXRApp::InitReceiver() {

    if (mReceiver) {
        LOGV("Receiver already created");
        return true;
    }

    mContext.type = cxrGraphicsContext_GLES;
    mContext.egl.display = eglGetCurrentDisplay();
    mContext.egl.context = eglGetCurrentContext();

    cxrReceiverDesc desc = { 0 };
    desc.requestedVersion = CLOUDXR_VERSION_DWORD;
    desc.deviceDesc = mDeviceDesc;
    desc.clientCallbacks = mClientCallbacks;
    desc.clientContext = this;
    desc.shareContext = &mContext;
    desc.numStreams = 2;
    desc.receiverMode = cxrStreamingMode_XR;
    desc.debugFlags = mOptions.mDebugFlags;
    desc.logMaxSizeKB = CLOUDXR_LOG_MAX_DEFAULT;
    desc.logMaxAgeDays = CLOUDXR_LOG_MAX_DEFAULT;
    cxrError err = cxrCreateReceiver(&desc, &mReceiver);
    if (err != cxrError_Success)
    {
        LOGE("Failed to create CloudXR receiver. Error %d, %s.", err, cxrErrorString(err));
        return false;
    }

    return true;
}

bool WaveCloudXRApp::Connect() {

    if (mConnected) {
        LOGE("Already connected");
        return true;
    }

    if (!mInited) {
        LOGE("CloudXR is not initialized.");
        return false;
    }

    if (mOptions.mServerIP.empty()) {
        LOGE("Server IP is not specified.");
        return false;
    }

    // try to establish connection
    cxrError err = cxrConnect(mReceiver, mOptions.mServerIP.c_str(), cxrConnectionFlags_ConnectAsync);
    if (err != cxrError_Success)
    {
        LOGE("Failed to sent connection request to %s. Error %d, %s.",
             mOptions.mServerIP.c_str(), (int)err, cxrErrorString(err));
        shutdownCloudXR();
        return false;
    }

    LOGV("Connection request sent. %s", mOptions.mServerIP.c_str());
    return true;
}

bool WaveCloudXRApp::UpdateFrame() {

    if (mPaused || !mInited) {
        return false;
    }

    // Fetch a video frame if available.
    bool frameValid = false;
    if (mReceiver)
    {
        if (mConnected)
        {
            cxrError frameErr = cxrLatchFrame(mReceiver, &mFramesLatched,
                                              cxrFrameMask_All, LATCHFRAME_TIMEOUT_MS);
            frameValid = (frameErr == cxrError_Success);
            if (!frameValid)
            {
                if (frameErr == cxrError_Frame_Not_Ready)
                    LOGW("LatchFrame failed, frame not ready for %d ms", LATCHFRAME_TIMEOUT_MS);
                else
                    LOGE("Error in LatchFrame [%0d] = %s", frameErr, cxrErrorString(frameErr));
            } else {

                // If network condition is bad, e.g. bitrate usage down below ~5Mbps
                // Frames received will vary frequently in size (~5 times in 1 second)
                // and crash here due to no read/write protection

                // You can add mutex to protect buffer recreation process and potentially increase latency
                // or comment this function call to avoid crash caused by frequent size change
                // Not recreating buffer to match incoming frame size might result in blurry display.
                
                if ( mFramesLatched.frames[0].widthFinal != mRenderWidth ||
                     mFramesLatched.frames[0].heightFinal != mRenderHeight ) {
                    LOGE("Receive frame %dx%d, buffer size %dx%d",
                         mFramesLatched.frames[0].widthFinal, mFramesLatched.frames[0].heightFinal,
                         mRenderWidth, mRenderHeight);
                    //RecreateFramebuffer(mFramesLatched.frames[0].widthFinal, mFramesLatched.frames[0].heightFinal);
                }
            }
        }
    }
    return frameValid;
}

bool WaveCloudXRApp::UpdateHMDPose(const WVR_PoseState_t hmdPose) {

    if (mPaused || !mInited) {
        return false;
    }

    // mutex
    {
        mCXRPoseState.hmd.pose.poseIsValid = hmdPose.isValidPose; //cxrTrue;
        mCXRPoseState.hmd.pose.deviceIsConnected = cxrTrue;
        mCXRPoseState.hmd.pose.trackingResult = cxrTrackingResult_Running_OK;

        mCXRPoseState.hmd.pose.deviceToAbsoluteTracking = Convert(hmdPose.poseMatrix);
        mCXRPoseState.hmd.pose.velocity = Convert(hmdPose.velocity);
        mCXRPoseState.hmd.pose.angularVelocity = Convert(hmdPose.angularVelocity);

        mHmdPose = hmdPose;
    }
    return true;
}

bool WaveCloudXRApp::UpdateDevicePose(const WVR_DeviceType type, const WVR_PoseState_t ctrlPose) {

    if (mPaused || !mInited) {
        return false;
    }

    // mutex
    {
        size_t idx = type == WVR_DeviceType_Controller_Left ? 0 : 1;
        if (!WVR_IsDeviceConnected(type))
        {
            mCXRPoseState.controller[idx].pose.poseIsValid = cxrFalse;
            mCXRPoseState.controller[idx].pose.deviceIsConnected = cxrFalse;
        } else {
            mCXRPoseState.controller[idx].pose.poseIsValid = ctrlPose.isValidPose; //cxrTrue;
            mCXRPoseState.controller[idx].pose.deviceIsConnected = cxrTrue;
            mCXRPoseState.controller[idx].pose.trackingResult = cxrTrackingResult_Running_OK;

            mCXRPoseState.controller[idx].pose.deviceToAbsoluteTracking = Convert(ctrlPose.poseMatrix);
            mCXRPoseState.controller[idx].pose.velocity = Convert(ctrlPose.velocity);
            mCXRPoseState.controller[idx].pose.angularVelocity = Convert(ctrlPose.angularVelocity);

            // Do not report buttons in case if input is captured by the system
            if (WVR_IsInputFocusCapturedBySystem())
            {
                mCXRPoseState.controller[idx].booleanComps = 0;
                mCXRPoseState.controller[idx].booleanCompsChanged = 0;
                memset(mCXRPoseState.controller[idx].scalarComps, 0, sizeof(mCXRPoseState.controller[idx].scalarComps));
            }
        }
    }
    return true;

    /*
     * Unhandled devices
        WVR_DeviceType_Camera                       = 4,
        WVR_DeviceType_EyeTracking                  = 5,
        WVR_DeviceType_HandGesture_Right            = 6,
        WVR_DeviceType_HandGesture_Left             = 7,
        WVR_DeviceType_NaturalHand_Right            = 8,
        WVR_DeviceType_NaturalHand_Left             = 9,
        WVR_DeviceType_ElectronicHand_Right         = 10,
        WVR_DeviceType_ElectronicHand_Left          = 11,
     */

}

bool WaveCloudXRApp::UpdateAnalog()
{
    if (mPaused || !mInited) {
        return false;
    }

    // Analog axis handling. Buttons will be handled in UpdateInput()
    const struct
    {
        WVR_DeviceType wvrId;
        cxrControllerId cvrId;
    }
            controllerMaps[] =
            {
                    {WVR_DeviceType_Controller_Left, cxrController_Left},
                    {WVR_DeviceType_Controller_Right, cxrController_Right}
            };

    const struct
    {
        WVR_InputId wvrId;
        cxrAnalogId cvrId;
    }
            axisMaps[] =
            {
                    {WVR_InputId_Alias1_Touchpad, cxrAnalog_TouchpadX},
                    {WVR_InputId_Alias1_Trigger, cxrAnalog_Trigger},
                    {WVR_InputId_Alias1_Grip, cxrAnalog_Grip},
                    {WVR_InputId_Alias1_Thumbstick, cxrAnalog_JoystickX},
            };

    uint32_t inputType = WVR_InputType_Button | WVR_InputType_Touch | WVR_InputType_Analog;
    uint32_t buttons = 0;
    uint32_t touches = 0;
    WVR_AnalogState_t analogState[3];

    for (auto controllerMap : controllerMaps)
    {
        auto& controller = mCXRPoseState.controller[controllerMap.cvrId];

        for (auto axisMap : axisMaps)
        {
            if (!WVR_IsDeviceConnected(controllerMap.wvrId))
                continue;

            // old
            // check if the axis available on this controller based on init query
            const int axisIndex = WVR_DeviceType_Controller_Left - controllerMap.wvrId; // left is larger value!
            //if (mAnalogAxisCaps[axisIndex] & (1<<axisMap.wvrId))
            //{
                auto axis = WVR_GetInputAnalogAxis(controllerMap.wvrId, axisMap.wvrId);
                controller.scalarComps[axisMap.cvrId] = axis.x;

                if (axisMap.cvrId == cxrAnalog_TouchpadX)
                    controller.scalarComps[cxrAnalog_TouchpadY] = axis.y;
                if (axisMap.cvrId == cxrAnalog_JoystickX)
                    controller.scalarComps[cxrAnalog_JoystickY] = axis.y;
                if (axisMap.cvrId == cxrAnalog_Grip)
                    controller.scalarComps[cxrAnalog_Grip_Force] = axis.y;
            //}
        }
    }

    return true;
}

bool WaveCloudXRApp::Render(const uint32_t eye, WVR_TextureParams_t eyeTexture, const bool frameValid) {

    if (frameValid) {
        // Submit frame with pose that render this frame
        auto& framePose = mFramesLatched.poseMatrix;
        WVR_Matrix4f_t headMatrix = Convert(framePose);
        WVR_ConvertMatrixQuaternion(&headMatrix, &mHmdPose.rawPose.rotation, true);

        cxrBool result = cxrBlitFrame(mReceiver, &mFramesLatched, 1 << eye);

        WVR_SubmitExtend ext = WVR_SubmitExtend_Default;
        WVR_SubmitFrame((WVR_Eye)eye, &eyeTexture, &mHmdPose, ext);

        if (eye == (uint32_t)WVR_Eye_Right) {
            cxrReleaseFrame(mReceiver, &mFramesLatched);
        }

    }
    // Render grey color gradient when frame invalid for whatever reason
    else {
        static const float ping = 0.0f;
        static const float pong = 0.4f;
        static const float step = 0.0025f;
        static float direction = 1.0f;
        static float color = ping;

        color += direction * step;
        if (color > pong || color < ping) direction *= -1.0f;

        // LOGD("loading %f", color);
        glClearColor(color, color, color, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        WVR_SubmitExtend ext = WVR_SubmitExtend_Default;
        WVR_SubmitFrame((WVR_Eye)eye, &eyeTexture, &mHmdPose, ext);
    }

    return true;
}

bool WaveCloudXRApp::UpdateInput(const WVR_Event_t& event)
{
    if (mPaused || !mInited) {
        return false;
    }

    // filter out non-controller events
    if (event.common.type < WVR_EventType_ButtonPressed || event.common.type > WVR_EventType_UpToDownSwipe) {
        return false;
    }

    const static WvrCxrButtonMapping WCButtonRemaps[] =
            {
                    { WVR_InputId_Alias1_Menu, cxrButton_ApplicationMenu, "Menu" },
                    { WVR_InputId_Alias1_Trigger, cxrButton_Trigger_Click, "Trigger" },
                    { WVR_InputId_Alias1_Grip, cxrButton_Grip_Click, "Grip" },
                    { WVR_InputId_Alias1_Touchpad, cxrButton_Touchpad_Click, "Touchpad" },
                    { WVR_InputId_Alias1_Thumbstick, cxrButton_Joystick_Click, "Thumb" },
                    { WVR_InputId_Alias1_A, cxrButton_A, "A" },
                    { WVR_InputId_Alias1_B, cxrButton_B, "B" },
                    { WVR_InputId_Alias1_X, cxrButton_X, "X" },
                    { WVR_InputId_Alias1_Y, cxrButton_Y, "Y" },
            };

    const size_t totalRemaps = sizeof(WCButtonRemaps)/sizeof(*WCButtonRemaps);

    uint32_t idx = (event.device.deviceType == WVR_DeviceType_Controller_Left) ? 0 : 1;
    auto& controllerState = mCXRPoseState.controller[idx];

    uint64_t inputMask = 0;
    const uint64_t prevComps = controllerState.booleanComps;
    int setSize = totalRemaps;

    bool handled = HandleButtonRemap(idx, controllerState, inputMask,
                                     event.input.inputId, event.common.type, WCButtonRemaps, setSize,
                                     (idx==0));
    if (!handled) return true;

    if (prevComps != controllerState.booleanComps)
        controllerState.booleanCompsChanged |= inputMask;
    else
        controllerState.booleanCompsChanged &= ~inputMask;

    return true;
}

bool WaveCloudXRApp::HandleButtonRemap(uint32_t idx, cxrControllerTrackingState &ctl, uint64_t &inputMask,
                                      WVR_InputId inId, WVR_EventType evType, const WvrCxrButtonMapping mappingSet[], int mapSize, bool left6dof=false)
{
    for(int i=0; i<mapSize; i++)
    {
        const WvrCxrButtonMapping &map = mappingSet[i];
        if (inId != map.wvrId) continue;

        inputMask = 1ULL << map.cxrId;
        // map left controller menu button to SteamVR system menu
        if (left6dof && map.cxrId == cxrButton_ApplicationMenu)
            inputMask = 1ULL << cxrButton_System;

        if (evType==WVR_EventType_ButtonPressed)
        {
            ctl.booleanComps |= inputMask;
            if (map.cxrId == cxrButton_Trigger_Click &&
                map.wvrId == WVR_InputId_Alias1_Trigger)
                ctl.scalarComps[cxrAnalog_Trigger] = 1.0f;
        }
        else if (evType==WVR_EventType_ButtonUnpressed)
        {
            ctl.booleanComps &= ~inputMask;
            if (map.cxrId == cxrButton_Trigger_Click &&
                map.wvrId == WVR_InputId_Alias1_Trigger)
                ctl.scalarComps[cxrAnalog_Trigger] = 0.0f;
        }
        else if (evType==WVR_EventType_TouchTapped)
        {
            inputMask = 0;
            if (map.wvrId == WVR_InputId_Alias1_Touchpad)
                inputMask = 1 << cxrButton_Touchpad_Touch;
            else if (map.wvrId == WVR_InputId_Alias1_Trigger)
                inputMask = 1 << cxrButton_Trigger_Touch;
            else if (map.wvrId == WVR_InputId_Alias1_Thumbstick)
                inputMask = 1 << cxrButton_Joystick_Touch;

            if (inputMask)
            {
                ctl.booleanComps |= inputMask;
            }
        }
        else if (evType==WVR_EventType_TouchUntapped && map.cxrId==cxrButton_Touchpad_Click)
        {
            inputMask = 0;
            if (map.wvrId == WVR_InputId_Alias1_Touchpad)
                inputMask = 1 << cxrButton_Touchpad_Touch;
            else if (map.wvrId == WVR_InputId_Alias1_Trigger)
                inputMask = 1 << cxrButton_Trigger_Touch;
            else if (map.wvrId == WVR_InputId_Alias1_Thumbstick)
                inputMask = 1 << cxrButton_Joystick_Touch;

            if (inputMask)
                ctl.booleanComps &= ~inputMask;
        }
        else
            return false; // we don't handle whatever it was!

#ifdef DEBUG_LOGGING
        LOGV("#> btn %s [%d], type=%d, state change %s {%llx}", map.nameStr, map.cxrId, evType,
             active ? "ACTIVE" : "inactive", ctl.booleanComps);
#endif

        return true;
    }

    return false;
}

/*
 * CloudXR callbacks
 *
 * */
oboe::DataCallbackResult WaveCloudXRApp::onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) {
    cxrAudioFrame recordedFrame{};
    recordedFrame.streamBuffer = (int16_t*)audioData;
    recordedFrame.streamSizeBytes = numFrames * CXR_AUDIO_CHANNEL_COUNT * CXR_AUDIO_SAMPLE_SIZE;
    cxrSendAudio(mReceiver, &recordedFrame);

    return oboe::DataCallbackResult::Continue;
}

void WaveCloudXRApp::GetTrackingState(cxrVRTrackingState *trackingState) {

    if (mPaused || !mConnected || nullptr == trackingState)
        return;

    // mutex?
    *trackingState = mCXRPoseState;
}

cxrBool WaveCloudXRApp::RenderAudio(const cxrAudioFrame *audioFrame) {
    if (!mPlaybackStream)
    {
        return cxrFalse;
    }

    const uint32_t timeout = audioFrame->streamSizeBytes / CXR_AUDIO_BYTES_PER_MS;
    const uint32_t numFrames = timeout * CXR_AUDIO_SAMPLING_RATE / 1000;
    uint32_t timeoutMS = 4*timeout; // WAR for oboe timing issue on Focus+.
    mPlaybackStream->write(audioFrame->streamBuffer, numFrames, timeoutMS * oboe::kNanosPerMillisecond);

    return cxrTrue;
}

void WaveCloudXRApp::TriggerHaptic(const cxrHapticFeedback *haptic) {

    if (!mConnected || !mInited)
        return;

    // Apply haptic feedback
    if (haptic->seconds <= 0)
        return;

    WVR_TriggerVibration(haptic->controllerIdx == 0 ?
                         WVR_DeviceType_Controller_Left : WVR_DeviceType_Controller_Right,
                         WVR_InputId_Max, static_cast<uint32_t>(haptic->seconds*1000000), 1,
                         WVR_Intensity_Normal);
}

// Shutdown streaming if paused
void WaveCloudXRApp::Pause() {
    if (!mPaused) {
        LOGW("Receive pause");
        mPaused = true;
        mInited = false;
        shutdownCloudXR();
    } else {
        // already paused, skip
    }
}

// ReInit & Reconnect to server when resumed
void WaveCloudXRApp::Resume() {
    if (mPaused) {
        LOGW("Receive resume");
        if (initCloudXR()) {
            Connect();
            mPaused = false;
        }
    } else {
        // already resumed, skip
    }
}

void WaveCloudXRApp::HandleClientState(cxrClientState state, cxrStateReason reason) {
    switch (state)
    {
        case cxrClientState_ReadyToConnect:
            LOGW("Client is ready to connect.");
            Connect();
            break;
        case cxrClientState_ConnectionAttemptInProgress:
            LOGW("Connecting ...");
            mConnected = false;
            break;
        case cxrClientState_StreamingSessionInProgress:
            LOGW("Connection established.");
            mConnected = true;
            break;
        case cxrClientState_ConnectionAttemptFailed:
            LOGE("Connection attempt failed. Reason: [%d]", reason);
            state = cxrClientState_Disconnected;
            mConnected = false;
            break;
        case cxrClientState_Disconnected:
            LOGE("Server disconnected with reason: [%d]", reason);
            mConnected = false;
            break;
        default:
            LOGW("Client state updated: %d to %d, reason: %d", mClientState, state, reason);
            break;
    }

    // if disconnected, for now just reset to exiting state, and let other layers
    // do the right thing to shut down.
    if (state==cxrClientState_Disconnected)
        state = cxrClientState_Exiting;

    // update the state of the app
    mClientState = state;
    mClientStateReason = reason;
}