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

#include "CloudXRMatrixHelpers.h"
#include "WaveCloudXRApp.h"

// Return micro second.  Should always positive because now is bigger.
#define timeval_subtract(now, last) \
    ((now.tv_sec - last.tv_sec) * 1000000LL + now.tv_usec - last.tv_usec)

#define VR_MAX_CLOCKS 200
#define LATCHFRAME_TIMEOUT_MS 100 // timeout to fetch a frame from CloudXR server
#define FRAME_TIMEOUT_SECOND 10.0f // retry connection if no valid frame or connection timeout

#define VERSION_CODE "v1.5"

#define CASE(x) \
case x:     \
return #x

const char* ClientStateEnumToString(cxrClientState state)
{
    switch (state)
    {
        CASE(cxrClientState_ReadyToConnect);
        CASE(cxrClientState_ConnectionAttemptInProgress);
        CASE(cxrClientState_ConnectionAttemptFailed);
        CASE(cxrClientState_StreamingSessionInProgress);
        CASE(cxrClientState_Disconnected);
        CASE(cxrClientState_Exiting);
        default:
            return "";
    }
}
#undef CASE
static constexpr int inputTouchLegacyCount = 21;

static const char* inputsTouchLegacy[inputTouchLegacyCount] =
        {
                "/input/system/click",
                "/input/application_menu/click",
                "/input/trigger/click",     // 2
                "/input/trigger/touch",     // 3
                "/input/trigger/value",     // 4
                "/input/grip/click",        // 5
                "/input/grip/touch",        // 6
                "/input/grip/value",        // 7
                "/input/joystick/click",    // 8
                "/input/joystick/touch",    // 9
                "/input/joystick/x",        // 10
                "/input/joystick/y",        // 11
                "/input/a/click",           // 12
                "/input/b/click",           // 13
                "/input/x/click",           // 14
                "/input/y/click",           // 15
                "/input/a/touch",           // 16
                "/input/b/touch",           // 17
                "/input/x/touch",           // 18
                "/input/y/touch",           // 19
                "/input/thumb_rest/touch",
        };

cxrInputValueType inputValuesTouchLegacy[inputTouchLegacyCount] =
        {
                cxrInputValueType_boolean, //input/system/click
                cxrInputValueType_boolean, //input/application_menu/click
                cxrInputValueType_boolean, //input/trigger/click
                cxrInputValueType_boolean, //input/trigger/touch
                cxrInputValueType_float32, //input/trigger/value
                cxrInputValueType_boolean, //input/grip/click
                cxrInputValueType_boolean, //input/grip/touch
                cxrInputValueType_float32, //input/grip/value
                cxrInputValueType_boolean, //input/joystick/click
                cxrInputValueType_boolean, //input/joystick/touch
                cxrInputValueType_float32, //input/joystick/x
                cxrInputValueType_float32, //input/joystick/y
                cxrInputValueType_boolean, //input/a/click
                cxrInputValueType_boolean, //input/b/click
                cxrInputValueType_boolean, //input/x/click
                cxrInputValueType_boolean, //input/y/click
                cxrInputValueType_boolean, //input/a/touch
                cxrInputValueType_boolean, //input/b/touch
                cxrInputValueType_boolean, //input/x/touch
                cxrInputValueType_boolean, //input/y/touch
                cxrInputValueType_boolean, //input/thumb_rest/touch
        };
WaveCloudXRApp::WaveCloudXRApp()
        : mTimeDiff(0.0f)
        , mLeftEyeQ(nullptr)
        , mRightEyeQ(nullptr)
        , mReceiver(nullptr)
        , mPlaybackStream(nullptr)
        , mRecordStream(nullptr)
        , mClientState(cxrClientState_ReadyToConnect)
        , mRenderWidth(1720)
        , mRenderHeight(1720)
        , mConnected(false)
        , mInited(false)
        , mPaused(true)
        , mStateDirty(true)
        {}

bool WaveCloudXRApp::initVR() {
    LOGI("Wave CloudXR Sample %s", VERSION_CODE);

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
    param = { WVR_GraphicsApiType_OpenGL, WVR_RenderConfig_Default };

    WVR_RenderError pError = WVR_RenderInit(&param);
    if (pError != WVR_RenderError_None) {
        LOGE("Present init failed - Error[%d]", pError);
    }

    LOGI("initVR done");
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
        mPlaybackStream = nullptr;
    }

    if (mRecordStream)
    {
        mRecordStream->close();
        mRecordStream = nullptr;
    }

    if (mReceiver) {
        cxrDestroyReceiver(mReceiver);
        mReceiver = nullptr;
    }

    mInited = false;
    LOGE("ShutdownCloudXR done");
}

bool WaveCloudXRApp::HandleCloudXRLifecycle(const bool pause)
{
    if (mPaused != pause) {
        if (pause) {
            Pause();
        } else {
            Resume();
        }
    }

    if (mStateDirty) {
        switch (mClientState) {
            case cxrClientState_ReadyToConnect:
                LOGI("Client is ready to connect.");
                Connect();
                break;
            case cxrClientState_Disconnected:
                if (mRetryConnCount < mMaxRetryConnCount) {
                    LOGE("Disconnected, reconnecting ... %d", mRetryConnCount);
                    shutdownCloudXR();
                    if (initCloudXR()) {
                        mRetryConnCount++;
                        Connect();
                    } else {
                        LOGE("Reinitialization failed, exiting app.");
                        return false;
                    }
                } else {
                    LOGE("Unrecoverable disconnection, exiting app. ");
                    return false;
                }
                break;
            case cxrClientState_Exiting:
                return false;

            default:
                break;
        }
        mStateDirty = false;
    }

    return true;
}


//-----------------------------------------------------------------------------
// Purpose: Poll events.  Quit application if return true.
//-----------------------------------------------------------------------------
bool WaveCloudXRApp::handleInput() {
    // Process WVR events
    WVR_Event_t event;
    while(WVR_PollEventQueue(&event)) {
        if(event.common.type == WVR_EventType_Quit) {
            shutdownCloudXR();
            return false;
        }

        processVREvent(event);
        UpdateInput(event);
    }
    UpdateAnalog();

    return true;
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

//-----------------------------------------------------------------------------
// Purpose: Processes a single VR event
//-----------------------------------------------------------------------------
void WaveCloudXRApp::processVREvent(const WVR_Event_t & event) {
    switch(event.common.type) {
    case WVR_EventType_IpdChanged:
        {
            WVR_RenderProps_t props;
            bool ret = WVR_GetRenderProps(&props);
            float ipd = 0;
            if (ret) {
                mDeviceDesc.ipd = ipd; // used when re-init
                mCXRPoseState.hmd.ipd = ipd; // updated along with pose
            }
            LOGI("Receive WVR_EventType_IpdChanged = %d", ipd);
        }
        break;
    case WVR_EventType_RenderingToBePaused:
    case WVR_EventType_DeviceSuspend:
        {
            LOGE("Device %d suspended.", event.device.deviceType);
            mPaused = true;
            break;
        }

    case WVR_EventType_RenderingToBeResumed:
    case WVR_EventType_DeviceResume:
        {
            LOGE("WVR Device %d resumed.", event.device.deviceType);
            mPaused = false;
            break;
        }

    default:
        break;
    }
}
bool WaveCloudXRApp::renderFrame() {
    updateTime();

    bool frameValid = UpdateFrame();
    if (!frameValid) {
        // Exit program when no valid frame for too long
        mFrameInvalidTime += mTimeDiff;
        if (mFrameInvalidTime > FRAME_TIMEOUT_SECOND) {
            LOGD("No valid frame for %f seconds", mFrameInvalidTime);
            return false;
        }
    } else {
        mFrameInvalidTime = 0.0f;

        CheckStreamQuality();
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
    return true;
}

static uint updatePoseCount = 0;
static uint getPoseCount = 0;
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
    if (mTimeAccumulator2S > 1000000) {
        mFPS = mFrameCount / (mTimeAccumulator2S / 1000000.0f);
        LOGI("FPS %2.0f, UpdatePose: %d, GetPose: %d", mFPS, updatePoseCount, getPoseCount);

        updatePoseCount = 0;
        getPoseCount = 0;
        mFrameCount = 0;
        mTimeAccumulator2S = 0;
    }
}

void  WaveCloudXRApp::beginPoseStream() {
    if (mPoseStream == nullptr) {
        mPoseStream = new std::thread(&WaveCloudXRApp::updatePose, this);
    }
}

void  WaveCloudXRApp::stopPoseStream() {
    if(mPoseStream!=nullptr) {
        mExitPoseStream = true;
        if(mPoseStream->joinable()) {
           mPoseStream->join();
           delete mPoseStream;
            mPoseStream = nullptr;
        }
    }
}
// 1 sec = 1,000ms = 1,000,000,000ns
void WaveCloudXRApp::updatePose() {
    // Update pose 250 per second by default
    int deno = (mDeviceDesc.posePollFreq == 0) ? 250 : mDeviceDesc.posePollFreq;
    long long int sleepNs = 1000000000 / deno;
    LOGI("PoseStream Update per %lldns", sleepNs);

    while (!mExitPoseStream) {

        while (mInited && mConnected)
        {
            std::lock_guard<std::mutex> lock(mPoseMutex);
            {
                WVR_PoseOriginModel pom = WVR_PoseOriginModel_OriginOnGround;
                // Returns immediately with latest pose
                WVR_GetPoseState(WVR_DeviceType_HMD, pom, 0, &mHmdPose);
                UpdateHMDPose(mHmdPose);

                pom = mIs6DoFHMD ? WVR_PoseOriginModel_OriginOnGround
                                 : WVR_PoseOriginModel_OriginOnHead_3DoF;

                WVR_GetPoseState(WVR_DeviceType_Controller_Left, pom, 0, &mCtrlPoses[0]);
                UpdateDevicePose(WVR_DeviceType_Controller_Left, mCtrlPoses[0]);

                WVR_GetPoseState(WVR_DeviceType_Controller_Right, pom, 0, &mCtrlPoses[1]);
                UpdateDevicePose(WVR_DeviceType_Controller_Right, mCtrlPoses[1]);
                updatePoseCount++;
            }
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleepNs));
        }
    }

    LOGI("PoseStream end");
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
            LOGE("Config file not found.");
        case ParseStatus_Fail:
        case ParseStatus_ExitRequested:
        case ParseStatus_BadVal:
            LOGE("Config file loading failed.");
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

    mClientCallbacks.UpdateClientState = [](void* context, cxrClientState state, cxrError error)
    {
        return reinterpret_cast<WaveCloudXRApp*>(context)->HandleClientState(context, state, error);
    };

    mClientCallbacks.LogMessage = [](void* context, cxrLogLevel level, cxrMessageCategory category, void* extra, const char* tag, const char* const messageText)
    {
        switch(level) {
            case cxrLogLevel::cxrLL_Verbose:
                //LOGV("[%s] %s", tag, messageText); // this opens up verbose decoder logging
                break;
            case cxrLogLevel::cxrLL_Info:
                LOGI("[%s] %s", tag, messageText);
                break;
            case cxrLogLevel::cxrLL_Debug:
                LOGD("[%s] %s", tag, messageText);
                break;
            case cxrLogLevel::cxrLL_Warning:
                LOGW("[%s] %s", tag, messageText);
                break;
            case cxrLogLevel::cxrLL_Critical:
            case cxrLogLevel::cxrLL_Error:
                LOGE("[%s] %s", tag, messageText);
                break;
            default:
                //LOGI("[%s] %s", tag, messageText);
                break;
        }
    };

    mClientCallbacks.clientContext = this;

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

    mDeviceDesc.numVideoStreamDescs = CXR_NUM_VIDEO_STREAMS_XR;
    for (uint32_t i = 0; i < mDeviceDesc.numVideoStreamDescs; i++) {
        mDeviceDesc.videoStreamDescs[i].format = cxrClientSurfaceFormat_RGB;
        mDeviceDesc.videoStreamDescs[i].width = mRenderWidth;
        mDeviceDesc.videoStreamDescs[i].height = mRenderHeight;
        mDeviceDesc.videoStreamDescs[i].fps = props.refreshRate;
        mDeviceDesc.videoStreamDescs[i].maxBitrate = mOptions.mMaxVideoBitrate;
    }

    mDeviceDesc.stereoDisplay = true;
    mDeviceDesc.maxResFactor = mOptions.mMaxResFactor;

    mDeviceDesc.ipd = props.ipdMeter;
    mDeviceDesc.receiveAudio = mOptions.mReceiveAudio;
    mDeviceDesc.sendAudio = mOptions.mSendAudio;
    mDeviceDesc.posePollFreq = 0;

    mDeviceDesc.disablePosePrediction = false;
    mDeviceDesc.angularVelocityInDeviceSpace = true;
    mDeviceDesc.foveatedScaleFactor = (mOptions.mFoveation < 100) ? mOptions.mFoveation : 0;
    mDeviceDesc.disableVVSync = false;

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

    LOGI("Device property stream#0: IPD: %f, FPS: %f, display %dx%d, play area %.2fx%.2f\n",
         mDeviceDesc.ipd, mDeviceDesc.videoStreamDescs[0].fps,
         mDeviceDesc.videoStreamDescs[0].width, mDeviceDesc.videoStreamDescs[0].height,
         mDeviceDesc.chaperone.playArea.v[0], mDeviceDesc.chaperone.playArea.v[1]);

    LOGI("Device property stream#1: IPD: %f, FPS: %f, display %dx%d, play area %.2fx%.2f\n",
         mDeviceDesc.ipd, mDeviceDesc.videoStreamDescs[1].fps,
         mDeviceDesc.videoStreamDescs[1].width, mDeviceDesc.videoStreamDescs[1].height,
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
    if (mContext.egl.context == nullptr) {
        LOGV("eglContext invalid");
        return false;
    }

    cxrReceiverDesc desc = { 0 };
    desc.requestedVersion = CLOUDXR_VERSION_DWORD;
    desc.deviceDesc = mDeviceDesc;
    desc.clientCallbacks = mClientCallbacks;
    desc.shareContext = &mContext;
    desc.debugFlags = mOptions.mDebugFlags | cxrDebugFlags_OutputLinearRGBColor;
    desc.logMaxSizeKB = CLOUDXR_LOG_MAX_DEFAULT;
    desc.logMaxAgeDays = CLOUDXR_LOG_MAX_DEFAULT;
    strncpy(desc.appOutputPath, "sdcard/CloudXR/logs/", CXR_MAX_PATH - 1); // log file path
    desc.appOutputPath[CXR_MAX_PATH-1] = 0; // ensure null terminated if string was too long.
    cxrError err = cxrCreateReceiver(&desc, &mReceiver);
    if (err != cxrError_Success)
    {
        LOGE("Failed to create CloudXR receiver. Error %d, %s.", err, cxrErrorString(err));
        return false;
    }

    LOGV("Receiver created!");
    return true;
}

bool WaveCloudXRApp::Connect(const bool async) {

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

    mConnectionDesc.async = async;
    mConnectionDesc.useL4S = mOptions.mUseL4S; // Low Latency, Low Loss, and Scalable Throughput
    mConnectionDesc.clientNetwork = mOptions.mClientNetwork;
    mConnectionDesc.topology = mOptions.mTopology;
    cxrError err = cxrConnect(mReceiver, mOptions.mServerIP.c_str(), &mConnectionDesc);
    std::string constr = async ? "Connection request sent" : "Connection" ;
    if (err != cxrError_Success) {
        LOGE("%s failed, %s. Error %d, %s.",
             constr.c_str(),
             mOptions.mServerIP.c_str(), (int) err, cxrErrorString(err));
        shutdownCloudXR();
        return false;
    }

    LOGV("%s success. %s", constr.c_str(), mOptions.mServerIP.c_str());
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
                else if (frameErr == cxrError_Not_Connected)
                    LOGW("LatchFrame failed, receiver no longer connected.");
                else
                    LOGE("Error in LatchFrame [%0d] = %s", frameErr, cxrErrorString(frameErr));
            } else {

                // CloudXR SDK 3.1.1:
                // If network condition is bad, e.g. bitrate usage down below ~5Mbps
                // Frames received will vary frequently in size (~5 times in 1 second)
                // and crash here due to no read/write protection

                // You can add mutex to protect buffer recreation process and potentially increase latency
                // or comment this function call to avoid crash caused by frequent size change
                // Not recreating buffer to match incoming frame size might result in blurry display.

                // CloudXR SDK 3.2:
                // Recreating frame buffer causes reconnection process failed after HMD idle/wakeup.
                // Reason: cxrStateReason_DeviceDescriptorMismatch.

                if ( mFramesLatched.frames[0].widthFinal != mRenderWidth ||
                     mFramesLatched.frames[0].heightFinal != mRenderHeight ) {
                    /*LOGE("Receive frame %dx%d, buffer size %dx%d",
                         mFramesLatched.frames[0].widthFinal, mFramesLatched.frames[0].heightFinal,
                         mRenderWidth, mRenderHeight);*/
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

    {
        if (mDeviceDesc.ipd != 0.0f)
        {
            // is this flag supposed to be cleared after CXR gets it?
            mCXRPoseState.hmd.flags = cxrHmdTrackingFlags_HasIPD;
            mCXRPoseState.hmd.ipd = mDeviceDesc.ipd;
        }

        mCXRPoseState.hmd.pose.poseIsValid = hmdPose.isValidPose; //cxrTrue;
        mCXRPoseState.hmd.pose.deviceIsConnected = cxrTrue;
        mCXRPoseState.hmd.pose.trackingResult = cxrTrackingResult_Running_OK;

        cxrMatrix34 mat = Convert(mHmdPose.poseMatrix);
        cxrMatrixToVecQuat(&mat, &mCXRPoseState.hmd.pose.position, &mCXRPoseState.hmd.pose.rotation);
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

            cxrMatrix34 mat = Convert(ctrlPose.poseMatrix);
            cxrMatrixToVecQuat(&mat, &mCXRPoseState.controller[idx].pose.position, &mCXRPoseState.controller[idx].pose.rotation);
            mCXRPoseState.controller[idx].pose.velocity = Convert(ctrlPose.velocity);
            mCXRPoseState.controller[idx].pose.angularVelocity = Convert(ctrlPose.angularVelocity);
        }
    }
    return true;
}

bool WaveCloudXRApp::UpdateAnalog()
{
    if (mPaused || !mInited) {
        return false;
    }

    if (!mConnected) {
        return false;
    }

    for(size_t hand = HAND_LEFT; hand <= HAND_RIGHT; ++hand) {

        WVR_DeviceType ctl = (hand == HAND_LEFT) ?
                             WVR_DeviceType_Controller_Left : WVR_DeviceType_Controller_Right;

        if(!WVR_IsDeviceConnected(ctl)) {
            continue;
        }

        uint32_t inputType = WVR_InputType_Button | WVR_InputType_Touch | WVR_InputType_Analog;
        uint32_t buttons = 0;
        uint32_t touches = 0;
        WVR_AnalogState_t analogState[3];
        uint32_t analogCount = (uint32_t)WVR_GetInputTypeCount(ctl, WVR_InputType_Analog);
        if (!WVR_GetInputDeviceState(ctl, inputType, &buttons, &touches, analogState, analogCount)) {
            continue;
        }
        int stateCount = sizeof(analogState) / sizeof(WVR_AnalogState_t);

        for(size_t buttonType = IDX_TRIGGER; buttonType <= IDX_THUMBSTICK; ++buttonType) {
            if (mUpdateAnalogs[hand][buttonType]) {
                for(int i = 0; i < stateCount; i++) {
                    if(analogState[i].id == WVR_InputId_Alias1_Trigger ||
                       analogState[i].id == WVR_InputId_Alias1_Grip) {
                        cxrControllerEvent &e = mCTLEvents[hand][mCTLEventCount[hand]];
                        e.inputValue.valueType = cxrInputValueType_float32;
                        // e.clientTimeNS = event.device.common.timestamp;
                        e.clientInputIndex = GetAnalogInputIndex(true, analogState[i].id);
                        e.inputValue.vF32 = analogState[i].axis.x;
                        mCTLEventCount[hand]++;

                        /*LOGE("[UpdateInput] %s, WVRInputId %d, %s, Analog %f, Timestamp %lu",
                             hand == HAND_LEFT ? "LEFT" : "RIGHT", analogState[i].id,
                             inputsTouchLegacy[e.clientInputIndex], e.inputValue.vF32, e.clientTimeNS);*/
                    } else if (analogState[i].id == WVR_InputId_Alias1_Thumbstick) {
                        cxrControllerEvent &e = mCTLEvents[hand][mCTLEventCount[hand]];
                        e.inputValue.valueType = cxrInputValueType_float32;
                        // e.clientTimeNS = event.device.common.timestamp;
                        e.clientInputIndex = 10; // "/input/joystick/x"
                        e.inputValue.vF32 = analogState[i].axis.x;
                        mCTLEventCount[hand]++;

                        cxrControllerEvent &e2 = mCTLEvents[hand][mCTLEventCount[hand]];
                        e2.inputValue.valueType = cxrInputValueType_float32;
                        // e.clientTimeNS = event.device.common.timestamp;
                        e2.clientInputIndex = 11; // "/input/joystick/y"
                        e2.inputValue.vF32 = analogState[i].axis.y;
                        mCTLEventCount[hand]++;

                        /*LOGE("[UpdateInput] %s, WVRInputId %d, %s, Analog (%f, %f), Timestamp %lu",
                             hand == HAND_LEFT ? "LEFT" : "RIGHT", analogState[i].id,
                             inputsTouchLegacy[e.clientInputIndex], e.inputValue.vF32, e2.inputValue.vF32 , e.clientTimeNS);*/
                    }
                }

            }
        }

        if (mCTLEventCount[hand] > 0) {
            cxrError err = cxrFireControllerEvents(mReceiver, mControllers[hand], mCTLEvents[hand], mCTLEventCount[hand]);
            if (err != cxrError_Success)
            {
                LOGE("[UpdateInput] cxrFireControllerEvents failed: %s", cxrErrorString(err));

            }
        }
        mCTLEventCount[hand] = 0;
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

        if (eye == (uint32_t)WVR_Eye_Right && mReceiver && mConnected) {
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

// Fire 1 input event once at a time
// checkme: update all button states at fixed freq and fire all button event at once
bool WaveCloudXRApp::UpdateInput(const WVR_Event_t& event)
{
    if (mPaused || !mInited) {
        return false;
    }

    if (!mConnected) {
        return false;
    }

    // filter out non-controller mCTLEvents
    /*if (event.common.type < WVR_EventType_ButtonPressed || event.common.type > WVR_EventType_UpToDownSwipe) {
        return false;
    }*/

    WVR_DeviceType ctl = event.device.deviceType;
    if (ctl != WVR_DeviceType_Controller_Left && ctl != WVR_DeviceType_Controller_Right) {
        return false;
    }

    uint8_t hand = (ctl == WVR_DeviceType_Controller_Left) ? HAND_LEFT : HAND_RIGHT;
    // Create CXR controller handle
    if (mControllers[hand] == nullptr) {
        if (!WVR_IsDeviceConnected(ctl)) {
            // device disconnected but have input event incoming?
            // continue;
            return false;
        }

        cxrControllerDesc desc = {};
        desc.id = ctl;
        desc.role = (hand == HAND_LEFT) ?
                    "cxr://input/hand/left" : "cxr://input/hand/right";
        desc.controllerName = "Oculus Touch";
        //desc.controllerName = "vive_focus3_controller"; // CXR server does not recognize this name
        // desc.controllerName = "VIVE FOCUS 3 Controller";
        desc.inputCount = inputTouchLegacyCount;
        desc.inputPaths = inputsTouchLegacy;
        desc.inputValueTypes = inputValuesTouchLegacy;
        cxrError e = cxrAddController(mReceiver, &desc, &mControllers[hand]);
        if (e!=cxrError_Success)
        {
            LOGE("[UpdateInput] Error adding controller: %s", cxrErrorString(e));
            //continue;
            return false;
        }
        LOGE("[UpdateInput] Added controller %s, %s", desc.controllerName, desc.role);
    } else {
        // Controller handle exist but device is actually disconnected
        if (!WVR_IsDeviceConnected(ctl)) {
            LOGE("[UpdateInput] Device %d is disconnected.", ctl);
            // destroy controller handle
        }
    }

    // button
    cxrControllerEvent &e = mCTLEvents[hand][mCTLEventCount[hand]];
    bool updateAnalog = true;
    switch (event.common.type) {
        case WVR_EventType_TouchTapped:{
            e.inputValue.vBool = cxrTrue;
            e.clientInputIndex = GetTouchInputIndex(true, event.input.inputId);
            break;
        }
        case WVR_EventType_TouchUntapped:{
            e.inputValue.vBool = cxrFalse;
            e.clientInputIndex = GetTouchInputIndex(false, event.input.inputId);
            break;
        }
        case WVR_EventType_ButtonPressed:{
            e.inputValue.vBool = cxrTrue;
            e.clientInputIndex = GetPressInputIndex(hand, true, event.input.inputId);
            break;
        }
        case WVR_EventType_ButtonUnpressed:{
            e.inputValue.vBool = cxrFalse;
            e.clientInputIndex = GetPressInputIndex(hand, false, event.input.inputId);
            break;
        }
        default:
            updateAnalog = false;
            break;
    }

    if (e.clientInputIndex >= inputTouchLegacyCount) {
        // skip unbinded input
        LOGE("[UpdateInput] skip unbinded input %s, WVRInputId %d, CXRInputIndex %d",
             hand == HAND_LEFT ? "LEFT" : "RIGHT", event.input.inputId,
             e.clientInputIndex);
        return false;
    }

    e.clientTimeNS = event.device.common.timestamp;
    e.inputValue.valueType = cxrInputValueType_boolean;
    mCTLEventCount[hand]++;

    // analog flag
    switch(event.input.inputId) {
        case WVR_InputId_Alias1_Trigger:
            mUpdateAnalogs[hand][IDX_TRIGGER] = updateAnalog;
            break;
        case WVR_InputId_Alias1_Grip:
            mUpdateAnalogs[hand][IDX_GRIP] = updateAnalog;
            break;
        case WVR_InputId_Alias1_Thumbstick:
            mUpdateAnalogs[hand][IDX_THUMBSTICK] = updateAnalog;
            break;
        default:
            break;
    }

    if (mCTLEventCount[hand] > 0) {
        cxrError err = cxrFireControllerEvents(mReceiver, mControllers[hand], mCTLEvents[hand], mCTLEventCount[hand]);
        /*LOGE("[UpdateInput] %s, WVRInputId %d, %s, %d, Timestamp %lu",
             hand == HAND_LEFT ? "LEFT" : "RIGHT", event.input.inputId,
             inputsTouchLegacy[e.clientInputIndex], e.inputValue.vBool, e.clientTimeNS);*/
        if (err != cxrError_Success)
        {
            LOGE("[UpdateInput] cxrFireControllerEvents failed: %s", cxrErrorString(err));
            // TODO: how to handle UNUSUAL API errors? might just return up.
        }
    }
    mCTLEventCount[hand] = 0;

    return true;
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

    // std::lock_guard<std::mutex> lock(mPoseMutex);
    *trackingState = mCXRPoseState;

    getPoseCount++;
}

cxrBool WaveCloudXRApp::RenderAudio(const cxrAudioFrame *audioFrame) {
    if (!mPlaybackStream || !mInited || !mConnected)
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

    // deviceID is not necessary VR controller, can be gamepad or anything
    // need to map this ID to WVR Device
    WVR_TriggerVibration(haptic->deviceID == 0 ?
                         WVR_DeviceType_Controller_Left : WVR_DeviceType_Controller_Right,
                         WVR_InputId_Max, static_cast<uint32_t>(haptic->seconds*1000000), 1,
                         WVR_Intensity_Normal);
}

void WaveCloudXRApp::Pause() {
    if (!mPaused) {
        LOGW("Receive pause");
        mPaused = true;
        shutdownCloudXR();
    } else {
        // already paused, skip
    }
}

void WaveCloudXRApp::Resume() {
    if (mPaused) {
        LOGW("Receive resume");
        mPaused = false;
    } else {
        // already resumed
    }
}

// This is called from CloudXR thread
void WaveCloudXRApp::HandleClientState(void* context, cxrClientState state, cxrError error) {
    switch (state)
    {
        case cxrClientState_ConnectionAttemptInProgress:
            LOGW("Connection attempt in progress.");
            mConnected = false;
            break;
        case cxrClientState_StreamingSessionInProgress:
            LOGW("Connection attempt succeeded.");
            mConnected = true;
            break;
        case cxrClientState_ConnectionAttemptFailed:
            LOGE("Connection attempt failed with error: %s", cxrErrorString(error));
            state = cxrClientState_Disconnected; // retry connection
            mConnected = false;
            break;
        case cxrClientState_Disconnected:
            LOGE("Server disconnected with error: [%s]", cxrErrorString(error));
            mConnected = false;
            break;
        default:
            LOGW("Client state updated: %s to %s, reason: %s", ClientStateEnumToString(mClientState), ClientStateEnumToString(state), cxrErrorString(error));
            break;
    }

    if (mClientState != state) {
        mClientState = state;

        mStateDirty = true;
    }
}

uint16_t WaveCloudXRApp::GetTouchInputIndex(const bool touched, const WVR_InputId wvrInputId) {

    uint16_t ret = 999;
    switch(wvrInputId) {
        case WVR_InputId_Alias1_Thumbstick:
            ret = 9; // "/input/joystick/touch"
            break;
        case WVR_InputId_Alias1_Trigger:
            ret = 3; // "/input/trigger/touch"
            break;
        case WVR_InputId_Alias1_Grip:
            ret = 6; // "/input/grip/touch"
            break;
        case WVR_InputId_Alias1_A:
            ret = 16; // "/input/a/touch"
            break;
        case WVR_InputId_Alias1_B:
            ret = 17; // "/input/b/touch"
            break;
        case WVR_InputId_Alias1_X:
            ret = 18; // "/input/x/touch"
            break;
        case WVR_InputId_Alias1_Y:
            ret = 19; // "/input/y/touch"
            break;
        //case WVR_InputId_Alias1_Menu:
        //case WVR_InputId_Alias1_System:
        default:
            break;
    }

    return ret;
}

uint16_t WaveCloudXRApp::GetPressInputIndex(const uint8_t hand, const bool pressed, const WVR_InputId wvrInputId) {

    uint16_t ret = 999;
    switch(wvrInputId) {
        case WVR_InputId_Alias1_Thumbstick:
            ret = 8; // "/input/joystick/click"
            break;
        case WVR_InputId_Alias1_Trigger:
            ret = 2; // "/input/trigger/click"
            break;
        case WVR_InputId_Alias1_Grip:
            ret = 5; // "/input/grip/click"
            break;
        // WVR only sends A/B
        case WVR_InputId_Alias1_A:
            if (hand == HAND_RIGHT) ret = 12; // "/input/a/click"
            if (hand == HAND_LEFT) ret = 14; // "/input/x/click"
            break;
        case WVR_InputId_Alias1_B:
            if (hand == HAND_RIGHT) ret = 13; // "/input/b/click"
            if (hand == HAND_LEFT) ret = 15; // "/input/y/click"
            break;
        case WVR_InputId_Alias1_X:
            ret = 14; // "/input/x/click"
            break;
        case WVR_InputId_Alias1_Y:
            ret = 15; // "/input/y/click"
            break;
        case WVR_InputId_Alias1_Menu:
            // ret = 1; // "/input/application_menu/click"
            ret = 0; // "/input/system/click"
            break;
        case WVR_InputId_Alias1_System:
            ret = 0; // "/input/system/click"
            break;
        default:
            break;
    }
    return ret;
}

uint16_t WaveCloudXRApp::GetAnalogInputIndex(const bool pressed, const WVR_InputId wvrInputId) {

    uint16_t ret = 999;
    switch(wvrInputId) {
        /*case WVR_InputId_Alias1_Thumbstick:
            ret = 10; // "/input/joystick/x"
            ret = 11; // "/input/joystick/y"
            break;*/
        case WVR_InputId_Alias1_Trigger:
            ret = 4; // "/input/trigger/click"
            break;
        case WVR_InputId_Alias1_Grip:
            ret = 7; // "/input/grip/click"
            break;
        default:
            break;
    }
    return ret;
}

void WaveCloudXRApp::CheckStreamQuality() {

    // Log connection stats every 3 seconds
    const int STATS_INTERVAL_SEC = 3;
    mFramesUntilStats--;
    cxrConnectionStats mStats = {};
    if (mFramesUntilStats <= 0 &&
        cxrGetConnectionStats(mReceiver, &mStats) == cxrError_Success)
    {
        // Capture the key connection statistics
        char statsString[64] = { 0 };
        snprintf(statsString, 64, "FPS: %6.1f    Bitrate (kbps): %5d    Latency (ms): %3d", mStats.framesPerSecond, mStats.bandwidthUtilizationKbps, mStats.roundTripDelayMs);

        // Turn the connection quality into a visual representation along the lines of a signal strength bar
        char qualityString[64] = { 0 };
        snprintf(qualityString, 64, "Connection quality: [%s]",
                 mStats.quality == cxrConnectionQuality_Bad ? "Bad" :
                 mStats.quality == cxrConnectionQuality_Poor ? "Poor" :
                 mStats.quality == cxrConnectionQuality_Fair ? "Fair" :
                 mStats.quality == cxrConnectionQuality_Good ? "Good" :
                 mStats.quality == cxrConnectionQuality_Excellent ? "Excellent" : "Invalid");

        // There could be multiple reasons for low quality however we show only the most impactful to the end user here
        char reasonString[64] = { 0 };
        if (mStats.quality <= cxrConnectionQuality_Fair)
        {
            if (mStats.qualityReasons == cxrConnectionQualityReason_EstimatingQuality)
            {
                snprintf(reasonString, 64, "Reason: Estimating quality");
            }
            else if (mStats.qualityReasons & cxrConnectionQualityReason_HighLatency)
            {
                snprintf(reasonString, 64, "Reason: High Latency (ms): %3d", mStats.roundTripDelayMs);
            }
            else if (mStats.qualityReasons & cxrConnectionQualityReason_LowBandwidth)
            {
                snprintf(reasonString, 64, "Reason: Low Bandwidth (kbps): %5d", mStats.bandwidthAvailableKbps);
            }
            else if (mStats.qualityReasons & cxrConnectionQualityReason_HighPacketLoss)
            {
                if (mStats.totalPacketsLost == 0)
                {
                    snprintf(reasonString, 64, "Reason: High Packet Loss (Recoverable)");
                }
                else
                {
                    snprintf(reasonString, 64, "Reason: High Packet Loss (%%): %3.1f", 100.0f * mStats.totalPacketsLost / mStats.totalPacketsReceived);
                }
            }
        }

        LOGI("%s    %s    %s", statsString, qualityString, reasonString);
        mFramesUntilStats = (int)mStats.framesPerSecond * STATS_INTERVAL_SEC;
    }
}

