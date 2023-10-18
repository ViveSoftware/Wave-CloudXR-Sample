//========= Copyright 2016-2021, HTC Corporation. All rights reserved. ===========

#pragma once
#include <string>
#include <vector>
#include <thread>

#include <GLES3/gl31.h>

#include "oboe/Oboe.h"

#include <wvr/wvr.h>
#include <wvr/wvr_render.h>
#include <wvr/wvr_device.h>
#include <wvr/wvr_projection.h>
#include <wvr/wvr_overlay.h>
#include <wvr/wvr_system.h>
#include <wvr/wvr_events.h>
#include <wvr/wvr_arena.h>
#include <wvr/wvr_types.h>
#include <CloudXRClient.h>
#include <CloudXRCommon.h>
#include <CloudXRClientOptions.h>

class WaveCloudXRApp : public oboe::AudioStreamDataCallback
{
public:
    WaveCloudXRApp();

    bool initVR();
    bool initGL();
    bool initCloudXR();
    void shutdownVR();
    void shutdownGL();
    void shutdownCloudXR();

    bool handleInput();
    bool HandleCloudXRLifecycle(const bool pause);
    void beginPoseStream();
    void stopPoseStream();
    void updatePose();
    bool renderFrame();

    // Audio interface
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream,
                                          void *audioData, int32_t numFrames) override;

    // CloudXR callback
    void GetTrackingState(cxrVRTrackingState* trackingState);
    void TriggerHaptic(const cxrHapticFeedback* haptic);
    cxrBool RenderAudio(const cxrAudioFrame*);
    void HandleClientState(cxrClientState state, cxrStateReason reason);

    /* CloudXR interfaces */
    /*
     * Send connection request to specified server IP
     * */
    bool Connect(const bool async = true);
protected:
    void Pause();
    void Resume();

protected:
    void updateTime();
    void processVREvent(const WVR_Event_t & event);

    void ReleaseFramebuffers();
    void RecreateFramebuffer(const uint32_t width, const uint32_t height);

    GLuint CreateGLFramebuffer(const GLuint texId);

// CloudXR function
protected:
    bool LoadConfig();
    bool InitAudio();
    bool InitCallbacks();
    bool InitDeviceDesc();
    bool InitReceiver();

    /*
     * Fetch video frame from CloudXR server
     */
    bool UpdateFrame();

    /*
     * Get device poses/inputs from WaveVR and update to CloudXR Server
     * */
    bool UpdateHMDPose(const WVR_PoseState_t hmdPose);
    bool UpdateDevicePose(const WVR_DeviceType type, const WVR_PoseState_t ctrlPose);
    bool UpdateInput(const WVR_Event_t& event);
    bool UpdateAnalog();

    /*
     * Render the video frame to the currently bound target surface
     */
    bool Render(const uint32_t eye, WVR_TextureParams_t eyeTexture, const bool frameValid);
private:

    struct WvrCxrButtonMapping
    {
        WVR_InputId wvrId;
        cxrButtonId cxrId;
        char nameStr[32];
    };

    bool HandleButtonRemap(uint32_t idx, cxrControllerTrackingState &ctl, uint64_t &inputMask,
                           WVR_InputId inId, WVR_EventType evType, const WvrCxrButtonMapping mappingSet[], int mapSize, bool left6dof);


    // CloudXR
    cxrFramesLatched mFramesLatched{};
    cxrReceiverHandle mReceiver= nullptr;
    cxrDeviceDesc mDeviceDesc{};
    cxrClientCallbacks mClientCallbacks;
    cxrGraphicsContext mContext;
    CloudXR::ClientOptions mOptions;
    cxrConnectionDesc mConnectionDesc = {};

    bool mStateDirty = false;
    cxrClientState mClientState = cxrClientState_ReadyToConnect;
    cxrStateReason mClientStateReason = cxrStateReason_NoError;

    // Audio
    oboe::AudioStream* mPlaybackStream= nullptr;
    oboe::AudioStream* mRecordStream= nullptr;

    // Pose
    std::mutex mPoseMutex;
    std::thread *mPoseStream = nullptr;
    bool mExitPoseStream = false;
    cxrVRTrackingState mCXRPoseState;
    WVR_PoseState_t mHmdPose;
    WVR_PoseState_t mCtrlPoses[2];

    bool mIs6DoFHMD = false;
    bool mIs6DoFController[2] = {false, false};

    bool mConnected;
    bool mPaused;
    bool mInited;

    // Render
    void* mLeftEyeQ;
    void* mRightEyeQ;

    std::vector<GLuint> mLeftEyeFBO;
    std::vector<GLuint> mRightEyeFBO;

    uint32_t mRenderWidth;
    uint32_t mRenderHeight;

    float mFrameInvalidTime = 0.0f;

    // Statistics
    float mTimeDiff;
    uint32_t mTimeAccumulator2S;  // add in micro second.
    struct timeval mRtcTime;
    int mFrameCount = 0;
    float mFPS = 0;
    uint32_t mClockCount = 0;

};