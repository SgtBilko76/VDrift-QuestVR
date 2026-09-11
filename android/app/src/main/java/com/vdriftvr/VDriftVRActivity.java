package com.vdriftvr;

import static android.system.Os.setenv;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * Host activity for VDrift VR. Modelled on QuakeQuest's GLES3JNIActivity:
 * a plain SurfaceView, and the native app thread does everything (OpenXR, GL, game).
 * The game data lives in /sdcard/VDriftVR, so the app needs all-files access.
 * A build made by tools/make_release.ps1 carries that data as assets/gamedata.zip
 * and unpacks it there on first run; otherwise it is pushed with adb.
 */
@SuppressLint("SdCardPath")
public class VDriftVRActivity extends Activity implements SurfaceHolder.Callback
{
    private static final String TAG = "VDriftVR";
    private static final int REQUEST_STORAGE = 2294;
    private static final int REQUEST_MANAGE_ALL_FILES = 2296;

    static
    {
        String manufacturer = Build.MANUFACTURER.toLowerCase(Locale.ROOT);
        if (manufacturer.contains("oculus")) {
            manufacturer = "meta";
        }
        try {
            System.loadLibrary("openxr_loader");
        } catch (Throwable e) {
            Log.e(TAG, "openxr_loader not loadable: " + e);
        }
        try {
            setenv("OPENXR_HMD", manufacturer, true);
        } catch (Exception e) {
            // ignore
        }
        System.loadLibrary("vdriftvr");
    }

    private SurfaceHolder mSurfaceHolder;
    private long mNativeHandle;
    private String mDataDir = "/sdcard/VDriftVR";
    private boolean mUnpacking;

    @Override
    protected void onCreate(Bundle icicle)
    {
        Log.v(TAG, "VDriftVRActivity::onCreate()");
        super.onCreate(icicle);

        SurfaceView view = new SurfaceView(this);
        setContentView(view);
        view.getHolder().addCallback(this);

        File ext = Environment.getExternalStorageDirectory();
        if (ext != null) {
            mDataDir = new File(ext, "VDriftVR").getAbsolutePath();
        }

        checkPermissionsAndInitialize();
    }

    private void checkPermissionsAndInitialize()
    {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.fromParts("package", getPackageName(), null));
                startActivityForResult(intent, REQUEST_MANAGE_ALL_FILES);
                return;
            }
        } else if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.WRITE_EXTERNAL_STORAGE,
                                              Manifest.permission.READ_EXTERNAL_STORAGE }, REQUEST_STORAGE);
            return;
        }
        create();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data)
    {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_ALL_FILES) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && Environment.isExternalStorageManager()) {
                create();
            } else {
                Log.e(TAG, "All-files access not granted; exiting");
                finishAffinity();
                System.exit(0);
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults)
    {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE && grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            create();
        } else {
            System.exit(0);
        }
    }

    /** The marker written once this build's bundled data is on disk. */
    private File dataStamp()
    {
        /* Stamp the unpacked tree with the identity of the data that produced
         * it, taken from the CRC the APK's own zip directory already holds for
         * assets/gamedata.zip. Reading it costs nothing - the entry is not
         * decompressed - and it changes exactly when the packed data changes.
         *
         * The version code is not enough. Data and code ship in the same APK
         * and the version rarely moves during development, so a rebuilt data
         * set was silently never unpacked: the game went on running against
         * whatever the first install of that version had left behind. A menu
         * gaining a control is invisible, and the control simply never appears.
         */
        long id = 0;

        try {
            java.util.zip.ZipFile apk = new java.util.zip.ZipFile(getPackageCodePath());
            ZipEntry entry = apk.getEntry("assets/gamedata.zip");

            if (entry != null) {
                id = entry.getCrc();
            }
            apk.close();
        } catch (Exception e) {
            Log.w(TAG, "could not read the data CRC: " + e);
        }

        if (id == 0) {
            // Data-less build, or the APK would not open: fall back to the
            // version, which is what this used to key on.
            try {
                id = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;
            } catch (Exception e) {
                // Keep 0: the stamp still works, it just will not tell builds apart.
            }
        }

        return new File(mDataDir, ".data-" + Long.toHexString(id));
    }

    /**
     * Unpack the game data bundled in the APK into the writable data dir.
     *
     * The data is 230 MB and cannot live inside the app: the game reaches it
     * through ordinary paths, and keeping it outside also means it survives
     * reinstalling, and that the cars and tracks fetched later by the in-game
     * download manager sit in the same tree as the ones that shipped.
     *
     * Only the game's own files are written. vr.cfg is laid down once and then
     * left alone, since it is meant to be edited, and nothing is ever deleted -
     * so anything downloaded since stays where it is. The stamp carries the
     * version code, so a new build refreshes its own files and an ordinary launch
     * skips all of this.
     *
     * Runs off the main thread: a couple of hundred megabytes of I/O in onCreate
     * is long enough for Android to put up the "isn't responding" dialog.
     */
    private void unpackGameData()
    {
        InputStream raw = null;
        try {
            raw = getAssets().open("gamedata.zip");
        } catch (java.io.FileNotFoundException e) {
            return;     // Data-less build; it was pushed with adb.
        } catch (Exception e) {
            Log.e(TAG, "gamedata.zip not readable: " + e);
            return;
        }

        final File root = new File(mDataDir);
        final long started = System.currentTimeMillis();
        long bytes = 0;
        int files = 0;

        ZipInputStream zip = null;
        try {
            zip = new ZipInputStream(raw);
            byte[] buf = new byte[65536];
            ZipEntry entry;

            while ((entry = zip.getNextEntry()) != null) {
                File dst = new File(root, entry.getName());

                // A zip can name its way out of the directory it unpacks into.
                if (!dst.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator)) {
                    Log.e(TAG, "refusing entry outside the data dir: " + entry.getName());
                    continue;
                }
                if (entry.isDirectory()) {
                    dst.mkdirs();
                    continue;
                }
                // vr.cfg is for the player to edit; seed it, never overwrite it.
                if (dst.getName().equals("vr.cfg") && dst.exists()) {
                    continue;
                }

                File parent = dst.getParentFile();
                if (parent != null) {
                    parent.mkdirs();
                }

                OutputStream out = null;
                try {
                    out = new FileOutputStream(dst);
                    int n;
                    while ((n = zip.read(buf)) > 0) {
                        out.write(buf, 0, n);
                        bytes += n;
                    }
                } finally {
                    try { if (out != null) out.close(); } catch (Exception ignored) {}
                }
                files++;
            }

            // Drop the stamps of older builds, then claim this one.
            File[] stale = root.listFiles();
            if (stale != null) {
                for (File f : stale) {
                    if (f.getName().startsWith(".data-")) {
                        f.delete();
                    }
                }
            }
            dataStamp().createNewFile();

            Log.v(TAG, "unpacked " + files + " files, " + (bytes / (1024 * 1024)) + " MB in "
                       + ((System.currentTimeMillis() - started) / 1000) + "s -> " + mDataDir);
        } catch (Exception e) {
            // No stamp is written, so the next launch tries again rather than
            // starting the game on half a data set.
            Log.e(TAG, "unpackGameData failed: " + e);
        } finally {
            try { if (zip != null) zip.close(); } catch (Exception ignored) {}
        }
    }

    private void create()
    {
        if (mNativeHandle != 0 || mUnpacking) {
            return;
        }
        new File(mDataDir).mkdirs();

        if (!dataStamp().exists()) {
            mUnpacking = true;
            new Thread(new Runnable() {
                @Override
                public void run()
                {
                    unpackGameData();
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run()
                        {
                            mUnpacking = false;
                            startNative();
                        }
                    });
                }
            }, "vdvr-unpack").start();
            return;
        }
        startNative();
    }

    private void startNative()
    {
        if (mNativeHandle != 0) {
            return;
        }
        Log.v(TAG, "data dir = " + mDataDir);
        mNativeHandle = VDriftVRLib.onCreate(this, mDataDir);
        // If the surface already exists (permissions, or unpacking), hand it over now.
        if (mSurfaceHolder != null) {
            VDriftVRLib.onSurfaceCreated(mNativeHandle, mSurfaceHolder.getSurface());
        }
    }

    /** Called from native code when the game wants to quit. */
    public void shutdown()
    {
        Log.v(TAG, "shutdown() requested by native code");
        finishAffinity();
        System.exit(0);
    }

    @Override
    protected void onStart()
    {
        Log.v(TAG, "VDriftVRActivity::onStart()");
        super.onStart();
        if (mNativeHandle != 0) {
            VDriftVRLib.onStart(mNativeHandle, this);
        }
    }

    @Override
    protected void onResume()
    {
        Log.v(TAG, "VDriftVRActivity::onResume()");
        super.onResume();
        if (mNativeHandle != 0) {
            VDriftVRLib.onResume(mNativeHandle);
        }
    }

    @Override
    protected void onPause()
    {
        Log.v(TAG, "VDriftVRActivity::onPause()");
        if (mNativeHandle != 0) {
            VDriftVRLib.onPause(mNativeHandle);
        }
        super.onPause();
    }

    @Override
    protected void onStop()
    {
        Log.v(TAG, "VDriftVRActivity::onStop()");
        if (mNativeHandle != 0) {
            VDriftVRLib.onStop(mNativeHandle);
        }
        super.onStop();
    }

    @Override
    protected void onDestroy()
    {
        Log.v(TAG, "VDriftVRActivity::onDestroy()");
        if (mSurfaceHolder != null && mNativeHandle != 0) {
            VDriftVRLib.onSurfaceDestroyed(mNativeHandle);
        }
        if (mNativeHandle != 0) {
            VDriftVRLib.onDestroy(mNativeHandle);
        }
        super.onDestroy();
        mNativeHandle = 0;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder)
    {
        Log.v(TAG, "VDriftVRActivity::surfaceCreated()");
        mSurfaceHolder = holder;
        if (mNativeHandle != 0) {
            VDriftVRLib.onSurfaceCreated(mNativeHandle, holder.getSurface());
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height)
    {
        Log.v(TAG, "VDriftVRActivity::surfaceChanged()");
        mSurfaceHolder = holder;
        if (mNativeHandle != 0) {
            VDriftVRLib.onSurfaceChanged(mNativeHandle, holder.getSurface());
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder)
    {
        Log.v(TAG, "VDriftVRActivity::surfaceDestroyed()");
        if (mNativeHandle != 0) {
            VDriftVRLib.onSurfaceDestroyed(mNativeHandle);
        }
        mSurfaceHolder = null;
    }
}
