//========= Copyright 2016-2021, HTC Corporation. All rights reserved. ===========

package com.htc.vr.samples.wavecloudxr;

import com.htc.vr.sdk.VRActivity;

import android.Manifest;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

public class MainActivity extends VRActivity {
    private static final String TAG = "WaveCloudXRJAVA";
    private final int PERMISSION_REQUEST_CODE = 1;
    private boolean mPermissionGranted = false;

    static {
        System.loadLibrary("WaveCloudXRJNI");
        System.loadLibrary("CloudXRClient");
    }
    public MainActivity() {
    }

    // Permission prompt is triggered by WVROverlayService. In order to use the service, WVR_Init must be called first, at native layer.
    // So the order is: JAVA onCreate() > Native WVR_Init() > JAVA requestPermission() > JAVA onRequestPermissionsResult > Native CloudXR Init
    @Override
    protected void onCreate(Bundle icicle) {
        super.onCreate(icicle);
        nativeInit(getResources().getAssets());

        try {
            PackageManager pm = getPackageManager();
            PackageInfo info = pm.getPackageInfo(getApplicationInfo().packageName, 0);
            Log.i(TAG, "Application Version: " + info.versionName + " code: " + info.versionCode);
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        }

        // Request storage permission for loading CloudXRLaunchOptions (required) & writing CloudXR log to sdcard
        // Request audio permission for microphone (optional)
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED ||
            ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[] {
                    Manifest.permission.WRITE_EXTERNAL_STORAGE,
                    Manifest.permission.RECORD_AUDIO
            }, PERMISSION_REQUEST_CODE);
            Log.w(TAG, "Permission request sent.");
        } else {
            mPermissionGranted = true;
            OnPermission();
        }
    }

    protected void OnPermission() {
        if (mPermissionGranted) {
            Log.e(TAG, "Permission granted.");
        } else {
            Log.e(TAG, "Failed to grant necessary permission. Aborting the program.");
            finish();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mPermissionGranted) nativeOnResume();

    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mPermissionGranted) nativeOnPause();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode == PERMISSION_REQUEST_CODE && grantResults != null && grantResults.length > 0) {
            // Write
            if (grantResults[0] != PackageManager.PERMISSION_GRANTED) {
                Log.e(TAG, "Storage permission denied. Perm: "+ permissions[0] +" Result: " + grantResults[0]);
                mPermissionGranted = false;
            } else {
                mPermissionGranted = true;
            }

            // Audio
            if (grantResults[2] != PackageManager.PERMISSION_GRANTED) {
                Log.e(TAG, "Audio permission denied. Unable to use microphone during this streaming session. Perm: "+ permissions[1] +" Result: " + grantResults[1]);
            }
        }

        OnPermission();
    }

    // JNI
    static native void nativeInit(AssetManager am);
    static native void nativeOnPause();
    static native void nativeOnResume();
}
