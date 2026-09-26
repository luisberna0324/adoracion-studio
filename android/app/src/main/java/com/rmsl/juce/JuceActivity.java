/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

package com.rmsl.juce;

import static android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;
import static android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;
import static android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.content.Intent;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.provider.Settings;
import android.view.View;

//==============================================================================
public class JuceActivity   extends Activity
{
    private WifiManager.MulticastLock remoteMulticastLock;
    private boolean remoteMulticastRequested;

    public String getRemoteDeviceName()
    {
        String name = null;
        if (Build.VERSION.SDK_INT >= 25)
        {
            try
            {
                name = Settings.Global.getString (getContentResolver(), Settings.Global.DEVICE_NAME);
            }
            catch (SecurityException ignored) {}
        }
        if (name != null && !name.trim().isEmpty())
            return name.trim();
        String manufacturer = Build.MANUFACTURER != null ? Build.MANUFACTURER.trim() : "";
        String model = Build.MODEL != null ? Build.MODEL.trim() : "";
        if (model.toLowerCase().startsWith (manufacturer.toLowerCase()))
            return model;
        return (manufacturer + " " + model).trim();
    }

    public void setRemoteMulticastEnabled (boolean enabled)
    {
        remoteMulticastRequested = enabled;
        if (enabled)
        {
            if (remoteMulticastLock == null)
            {
                WifiManager manager = (WifiManager) getApplicationContext().getSystemService (Context.WIFI_SERVICE);
                if (manager != null)
                {
                    remoteMulticastLock = manager.createMulticastLock ("PlayWorship remote discovery");
                    remoteMulticastLock.setReferenceCounted (false);
                }
            }
            if (remoteMulticastLock != null && !remoteMulticastLock.isHeld())
                remoteMulticastLock.acquire();
        }
        else if (remoteMulticastLock != null && remoteMulticastLock.isHeld())
        {
            remoteMulticastLock.release();
        }
    }

    private native void startJUCEApplication();
    private native void onAuthCallback (String callbackUrl);

    private void initEdgeToEdge()
    {
        if (Build.VERSION.SDK_INT < 35)
        {
            View decorView = getWindow().getDecorView();

            final int flags = Build.VERSION.SDK_INT < 30
                    ? (  SYSTEM_UI_FLAG_LAYOUT_STABLE
                       | SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                       | SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
                    : 0;

            decorView.setSystemUiVisibility (decorView.getSystemUiVisibility() | flags);
        }

        if (30 <= Build.VERSION.SDK_INT)
            getWindow().setDecorFitsSystemWindows (false);

        if (29 <= Build.VERSION.SDK_INT)
        {
            if (Build.VERSION.SDK_INT < 35)
                getWindow().setStatusBarContrastEnforced (false);

            getWindow().setNavigationBarContrastEnforced (false);
        }
    }

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        initEdgeToEdge();

        super.onCreate (savedInstanceState);
        startJUCEApplication();
        if (getIntent().getData() != null && "playworship".equals(getIntent().getData().getScheme()))
            onAuthCallback (getIntent().getData().toString());
    }

    @Override
    protected void onNewIntent (Intent intent)
    {
        super.onNewIntent (intent);
        setIntent (intent);
        if (intent.getData() != null && "playworship".equals(intent.getData().getScheme()))
            onAuthCallback (intent.getData().toString());

        // Notification intent handling is disabled for this standalone target.
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        if (remoteMulticastRequested)
            setRemoteMulticastEnabled (true);
        // Notification resume handling is disabled for this standalone target.
    }

    @Override
    protected void onPause()
    {
        if (remoteMulticastLock != null && remoteMulticastLock.isHeld())
            remoteMulticastLock.release();
        super.onPause();
    }
}
