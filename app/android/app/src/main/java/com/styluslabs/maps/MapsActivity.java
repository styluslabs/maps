package com.styluslabs.maps;

import java.io.File;
import java.io.IOException;
import java.util.Collections;
import java.util.Map;
import java.util.HashMap;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.FileProvider;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.app.Activity;
import android.app.AlertDialog;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.os.PowerManager;
import android.net.Uri;
import android.util.Log;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.graphics.Color;
import android.graphics.Rect;
import android.view.WindowManager;
import android.view.View;
import android.view.ViewGroup;
import android.view.KeyEvent;
import android.view.Window;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.widget.EditText;
import android.widget.RelativeLayout;
import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.DialogInterface;
import android.content.BroadcastReceiver;
import android.content.IntentFilter;
import android.location.Location;
import android.location.LocationManager;
import android.location.LocationListener;
import android.location.GnssStatus;
import android.location.GpsStatus;
import android.location.GpsSatellite;
import android.hardware.SensorManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.GeomagneticField;
import com.mapzen.tangram.FontConfig;
import com.mapzen.tangram.networking.HttpHandler;
import com.mapzen.tangram.networking.DefaultHttpHandler;


public class MapsActivity extends Activity implements GpsStatus.Listener, LocationListener, SensorEventListener
{
  MapsView mMapsView;
  private ViewGroup mLayout;
  private DummyEdit mTextEdit;
  private LocationManager locationManager;
  private SensorManager mSensorManager;
  private GnssStatus.Callback mGnssStatusCallback;
  //private Sensor mAccelSensor;
  //private Sensor mMagSensor;
  private Sensor mOrientSensor;
  private HttpHandler httpHandler;
  private final Map<Long, Object> httpRequestHandles = Collections.synchronizedMap(new HashMap<Long, Object>());
  private float mDeclination = 0;
  private boolean replaceAssets = true;  // for development
  private boolean sensorsEnabled = true;
  private boolean hasGpsFix = false;

  public static final int PERM_REQ_LOCATION = 1;
  public static final int PERM_REQ_NOTIFICATIONS = 2;
  public static final int PERM_REQ_MEDIA = 3;

  @Override
  protected void onCreate(Bundle icicle)
  {
    super.onCreate(icicle);

    // this doesn't seem to detect problems in dependencies (e.g. OkHttp)
    //android.os.StrictMode.setVmPolicy(new android.os.StrictMode.VmPolicy.Builder() ...

    // creation of folder in media dir on first run randomly fails sometimes (on Samsung only?); also, search
    //  indexing performance (and I assume I/O performance in general) is much worse (10x worse on Pixel 3)
    String extMediaPath = getExternalMediaDirs()[0].getAbsolutePath() + "/files";
    String extFilesPath = getExternalFilesDir(null).getAbsolutePath();
    File mediacfgfile = new File(extMediaPath + "/config.yaml");
    String extPath = mediacfgfile.exists() ? extMediaPath : extFilesPath;

    MapsLib.init(this, getAssets(), extPath, BuildConfig.VERSION_CODE);

    httpHandler = new DefaultHttpHandler();

    // stackoverflow.com/questions/1513485 ; github.com/streetcomplete/StreetComplete ... FineLocationManager.kt
    locationManager = (LocationManager) getSystemService(LOCATION_SERVICE);

    // stackoverflow.com/questions/20339942 ; github.com/streetcomplete/StreetComplete ... Compass.kt
    mSensorManager = (SensorManager)getSystemService(SENSOR_SERVICE);
    //mAccelSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
    //mMagSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
    mOrientSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);

    if(!canGetLocation()) {
      requestPermissions(//this,  //ActivityCompat
          new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, PERM_REQ_LOCATION);
    }
    if(Build.VERSION.SDK_INT >= 24) {
      mGnssStatusCallback = new GnssStatus.Callback() {
        @Override
        public void onSatelliteStatusChanged(GnssStatus status) { onSatelliteStatus(status); }
      };
    }

    mMapsView = new MapsView(getApplication());
    mLayout = new RelativeLayout(this);
    mLayout.addView(mMapsView);
    mTextEdit = new DummyEdit(this);
    mLayout.addView(mTextEdit, new RelativeLayout.LayoutParams(0, 0));
    setContentView(mLayout);
    setEdgeToEdge();
    //setContentView(mMapsView);
    //mGLSurfaceView.setRenderMode(MapsView.RENDERMODE_WHEN_DIRTY);
    onNewIntent(getIntent());

    // detect location enabled/disabled
    /*registerReceiver(new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
          if(LocationManager.PROVIDERS_CHANGED_ACTION.equals(intent.getAction())) {
            if(!locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
              MapsLib.updateGpsStatus(-1, 0);  // hide GPS status icon
              hasGpsFix = false;
            }
          }
        }
      }, new IntentFilter(LocationManager.PROVIDERS_CHANGED_ACTION));*/

    // detect low power mode
    registerReceiver(new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
          PowerManager pm = (PowerManager)getSystemService(Context.POWER_SERVICE);
          MapsLib.onLowPower(pm.isPowerSaveMode() ? 1 : 0);
        }
      }, new IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED));
  }

  @Override
  protected void onNewIntent(Intent intent)  // refs: Write MainActivity.java
  {
    if(intent.getAction().equals(Intent.ACTION_VIEW) && intent.getScheme().equals("geo"))
      MapsLib.handleUri(intent.getDataString());
    else
      super.onNewIntent(intent);
  }

  @Override
  public void onRequestPermissionsResult(int requestCode, String permissions[], int[] grantResults) {
    switch (requestCode) {
    case PERM_REQ_LOCATION:
      if(grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED && canGetLocation())
        startSensors();
      break;
    case PERM_REQ_NOTIFICATIONS:
      if(grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED)
        setServiceState(1, 0.1f, 0);
      break;
    case PERM_REQ_MEDIA:
      if(grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED)
        pickFolder(true);
      break;
    }
  }

  // ref: SDLActivity.java
  protected void setEdgeToEdge()
  {
    Window window = getWindow();
    window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);
    // see setDecorFitsSystemWindows() in androidx/core/view/WindowCompat.java
    window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);  // | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
    //    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);  // to disable back gesture
    //if(Build.VERSION.SDK_INT >= 29)
    //  window.getDecorView().setSystemGestureExclusionRects(Collections.singletonList(new Rect(0, 0, 10000, 10000)));
    window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        //| WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);  -- breaks layout adjustment for keyboard
    window.setStatusBarColor(Color.TRANSPARENT);
    window.setNavigationBarColor(Color.TRANSPARENT);
  }

  /* add this to MapsView.java if setSystemGestureExclusionRects on decor view doesn't work:
  @Override
  protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
    super.onLayout(changed, left, top, right, bottom);
    if(Build.VERSION.SDK_INT >= 29)
      setSystemGestureExclusionRects(Collections.singletonList(new Rect(left, top, right, bottom)));
  }
  */

  protected boolean canGetLocation()
  {
    return checkSelfPermission(//this,  //ContextCompat
        Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
  }

  @Override
  protected void onPause()
  {
    super.onPause();
    if(sensorsEnabled)
      stopSensors();
    MapsLib.onPause();
  }

  @Override
  protected void onResume()
  {
    super.onResume();
    if(sensorsEnabled)
      startSensors();
    MapsLib.onResume();
  }

  // refs:
  // - https://github.com/streetcomplete/StreetComplete/blob/master/app/src/main/java/de/westnordost/streetcomplete/util/location/FineLocationManager.kt#L92
  //  - rejecting newer but less accurate locations; using NETWORK_PROVIDER
  // - https://github.com/barbeau/gpstest/blob/master/library/src/main/java/com/android/gpstest/library/data/SharedLocationManager.kt#L71
  // - https://github.com/osmandapp/OsmAnd/blob/master/OsmAnd/src/net/osmand/plus/helpers/AndroidApiLocationServiceHelper.java#L58
  // - https://github.com/organicmaps/organicmaps/blob/master/android/app/src/main/java/app/organicmaps/util/LocationUtils.java#L85
  public void startSensors()
  {
    // looks like you may need to use Play Services (or LocationManagerCompat?) for fused location prior to API 31 (Android 12)
    // - see https://developer.android.com/training/location/request-updates
    if(canGetLocation()) {
      // requesting updates from just fused provider doesn't turn on GPS!
      // min GPS dt = 0 (ms), dr = 1 (meters)
      try {  // looks like requestLocationUpdates() might throw on some devices if location disabled
        locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0, 1, this);
        //locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 0, 1, this);
        if(locationManager.getProvider("fused") != null) {
          locationManager.requestLocationUpdates("fused", 0, 1, this);
          onLocationChanged(locationManager.getLastKnownLocation("fused"));
        }
        else
          onLocationChanged(locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER));
        if(mGnssStatusCallback != null)
          locationManager.registerGnssStatusCallback(mGnssStatusCallback);  //catch (SecurityException e)
        else {
          onGpsStatusChanged(0);
          locationManager.addGpsStatusListener(this);  //catch (SecurityException e)
        }
      } catch(Exception e) {
        Log.v("Tangram", "Error requesting location updates", e);
      }
    }
    //mSensorManager.registerListener(this, mAccelSensor, SensorManager.SENSOR_DELAY_UI);
    //mSensorManager.registerListener(this, mMagSensor, SensorManager.SENSOR_DELAY_UI);
    mSensorManager.registerListener(this, mOrientSensor, SensorManager.SENSOR_DELAY_UI);

    PowerManager pm = (PowerManager)getSystemService(Context.POWER_SERVICE);
    MapsLib.onLowPower(pm.isPowerSaveMode() ? 1 : 0);
  }

  public void stopSensors()
  {
    if(canGetLocation()) {
      locationManager.removeUpdates(this);
      if(mGnssStatusCallback != null)
        locationManager.unregisterGnssStatusCallback(mGnssStatusCallback);
      else
        locationManager.removeGpsStatusListener(this);
    }
    mSensorManager.unregisterListener(this);
  }

  public void setSensorsEnabled(boolean enabled)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(sensorsEnabled && !enabled)
        stopSensors();
      else if(!sensorsEnabled && enabled)
        startSensors();
      sensorsEnabled = enabled;
     } });
  }

  private void showNotificationRationale()
  {
    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Light_Dialog_Alert)
        .setTitle("Enable Notifications")
        .setMessage("Notification permission must be enabled to record a track.")
        .setPositiveButton("OK", new DialogInterface.OnClickListener() {
          public void onClick(DialogInterface dialog, int which) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, PERM_REQ_NOTIFICATIONS);
          }
        })
        //.setIcon(android.R.drawable.ic_dialog_alert)
        .show();
  }

  private void showBatteryOptimizationRationale()
  {
    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Light_Dialog_Alert)
        .setTitle("Disable Battery Optimization")
        .setMessage("Battery optimization must be disabled to record a track.")
        .setPositiveButton("OK", new DialogInterface.OnClickListener() {
          public void onClick(DialogInterface dialog, int which) {
            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            intent.setData(Uri.parse("package:" + getPackageName()));
            try {
              startActivity(intent);
            } catch (Exception e) {  //ActivityNotFoundException
              // try ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS instead?
              //Toast.makeText(this, "Unable to open Battery settings", Toast.LENGTH_LONG).show();
            }
          }
        })
        //.setIcon(android.R.drawable.ic_dialog_alert)
        .show();
  }

  public void setServiceState(int state, float intervalSec, float minDist)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
        if(shouldShowRequestPermissionRationale(Manifest.permission.POST_NOTIFICATIONS))
          showNotificationRationale();
        else
          requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, PERM_REQ_NOTIFICATIONS);
        return;  // cannot start service w/o notification permission (?)
      }
      PowerManager pm = (PowerManager)getSystemService(Context.POWER_SERVICE);
      if(!pm.isIgnoringBatteryOptimizations(getPackageName()))
        showBatteryOptimizationRationale();  // we can still continue to start service

      if(state > 0) {
        Intent intent = new Intent(getApplicationContext(), MapsService.class).setAction(MapsService.START_RECORDING);
        intent.putExtra(MapsService.EXTRA_INTERVAL, intervalSec);
        intent.putExtra(MapsService.EXTRA_DISTANCE, minDist);
        if(Build.VERSION.SDK_INT >= 26)
          startForegroundService(intent);
        else
          startService(intent);
      }
      else {
        startService(new Intent(getApplicationContext(), MapsService.class).setAction(MapsService.STOP_RECORDING));
      }
     } });
  }

  public static void updateLocation(Location loc)
  {
    //Log.v("Tangram", loc.toString());
    double lat = loc.getLatitude();  // degrees
    double lng = loc.getLongitude();  // degrees
    float poserr = loc.getAccuracy();  // accuracy in meters
    double alt = loc.getAltitude();  // meters
    float alterr = loc.getVerticalAccuracyMeters();  // altitude accuracy in meters
    float dir = loc.hasBearing() ? loc.getBearing() : Float.NaN;  // bearing (direction of travel) in degrees
    float direrr = loc.getBearingAccuracyDegrees();
    float spd = loc.hasSpeed() ? loc.getSpeed() : Float.NaN;  // m/s
    float spderr = loc.getSpeedAccuracyMetersPerSecond();  // speed accuracy in m/s
    long time = loc.getTime();  // ms since unix epoch
    MapsLib.updateLocation(time, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr);
  }

  // LocationListener
  @Override
  public void onLocationChanged(Location loc)
  {
    if(loc == null) return;  // getLastKnownLocation() can return null
    if(hasGpsFix && loc.getProvider().equals("fused")) return;
    // for correcting orientation - convert degrees to radians
    mDeclination = new GeomagneticField((float)loc.getLatitude(), (float)loc.getLongitude(),
        (float)loc.getAltitude(), loc.getTime()).getDeclination()*(float)java.lang.Math.PI/180;
    updateLocation(loc);
  }

  @Override
  public void onProviderEnabled (String provider) {}

  @Override
  public void onProviderDisabled (String provider) {
    if(LocationManager.GPS_PROVIDER.equals(provider)) {
      MapsLib.updateGpsStatus(-1, 0);  // hide GPS status icon
      hasGpsFix = false;
    }
  }

  @Override
  public void onStatusChanged (String provider, int status, Bundle extras) {}  // deprecated in API 29

  // GpsStatus.Listener
  // see https://gitlab.com/mvglasow/satstat ... MainActivity.java
  @Override
  public void onGpsStatusChanged(int event)
  {
    GpsStatus status = locationManager.getGpsStatus(null);
    int satsVisible = 0;
    int satsUsed = 0;
    for(GpsSatellite sat : status.getSatellites()) {
      satsVisible++;
      if(sat.usedInFix())
        satsUsed++;
    }
    hasGpsFix = satsUsed > 0;
    MapsLib.updateGpsStatus(satsVisible, satsUsed);
  }

  //@TargetApi(Build.VERSION_CODES.N)
  public void onSatelliteStatus(GnssStatus status)
  {
    int satsUsed = 0;
    int satsVisible = status.getSatelliteCount();
    for(int ii = 0; ii < satsVisible; ii++) {
      if(status.usedInFix(ii))
        satsUsed++;
    }
    hasGpsFix = satsUsed > 0;
    MapsLib.updateGpsStatus(satsVisible, satsUsed);
  }
  //private float[] mGravity;
  //private float[] mGeomagnetic;

  @Override
  public void onSensorChanged(SensorEvent event)
  {
    // refs: github.com/barbeau/gpstest
    if(event.sensor.getType() == Sensor.TYPE_ROTATION_VECTOR) {
      float rotmat[] = new float[9];  //or 16
      float orient[] = new float[3];
      float values[] = new float[4];
      System.arraycopy(event.values, 0, values, 0, 4);
      SensorManager.getRotationMatrixFromVector(rotmat, values);
      SensorManager.getOrientation(rotmat, orient);
      MapsLib.updateOrientation(event.timestamp, orient[0] + mDeclination, orient[1], orient[2]);
    }

    //if(event.sensor.getType() == Sensor.TYPE_ACCELEROMETER)
    //  mGravity = event.values;

    //if(event.sensor.getType() == Sensor.TYPE_MAGNETIC_FIELD)
    //  mGeomagnetic = event.values;

    //if(mGravity != null && mGeomagnetic != null) {
    //  float R[] = new float[9];
    //  float I[] = new float[9];
    //  if(SensorManager.getRotationMatrix(R, I, mGravity, mGeomagnetic)) {
    //    // orientation contains azimut, pitch and roll
    //    float orient[] = new float[3];
    //    SensorManager.getOrientation(R, orient);
    //    MapsLib.updateOrientation(orient[0] + mDeclination, orient[1], orient[2]);
    //    //Log.v("Tangram", "Orientation from mag field: " + (180.0/java.lang.Math.PI)*(orient[0] + mDeclination));
    //  }
    //}
  }

  public void onAccuracyChanged(Sensor sensor, int accuracy) {}

  // required for tangram AndroidPlatform.cpp
  public void requestRender() { /*mGLSurfaceView.requestRender();*/  }
  public void setRenderMode(int cont) { /*mGLSurfaceView.setRenderMode(...);*/ }

  public String getFontFilePath(final String key)
  {
    return FontConfig.getFontFile(key);
  }

  public String getFontFallbackFilePath(final int importance, final int weightHint)
  {
    return FontConfig.getFontFallback(importance, weightHint);
  }

  public void cancelUrlRequest(final long requestHandle)
  {
    if(requestHandle == -1) {
      httpHandler.cancelAllRequests();
      httpRequestHandles.clear();
      return;
    }

    // handle will be removed by HttpHandler.Callback
    Object request = httpRequestHandles.get(requestHandle);
    if (request != null) {
      httpHandler.cancelRequest(request);
    }
  }

  public void startUrlRequest(@NonNull final String url,
      @NonNull final String headers, @NonNull final String payload, final long requestHandle)
  {
    final HttpHandler.Callback callback = new HttpHandler.Callback() {
      @Override
      public void onFailure(@Nullable final IOException e) {
        if (httpRequestHandles.remove(requestHandle) == null) { return; }
        String msg = (e == null) ? "" : e.getMessage();
        MapsLib.onUrlComplete(requestHandle, null, msg);
        //Log.v("Tangram", "Got failure " + msg + " for " + url);
      }

      @Override
      public void onResponse(final int code, @Nullable final byte[] rawDataBytes) {
        //Log.v("Tangram", "Got response " + code + " for " + url);
        if (httpRequestHandles.remove(requestHandle) == null) { return; }
        if (code >= 200 && code < 300) {
          MapsLib.onUrlComplete(requestHandle, rawDataBytes, null);
        } else {
          MapsLib.onUrlComplete(requestHandle, null,
                  "Unexpected response code: " + code + " for URL: " + url);
        }
      }

      @Override
      public void onCancel() {
        if (httpRequestHandles.remove(requestHandle) == null) { return; }
        MapsLib.onUrlComplete(requestHandle, null, null);
      }
    };

    //Log.v("Tangram", "Starting request for " + url);
    Object request = httpHandler.startRequest(url, headers, payload, callback);
    if (request != null) {
      httpRequestHandles.put(requestHandle, request);
    }
  }

  public void extractAssets(String outpath)
  {
    extractAssetPath(getAssets(), "", outpath);
  }

  // assetpath = "" reads from assets/
  private boolean extractAssetPath(AssetManager assetManager, String assetpath, String outpath)
  {
    try {
      if(assetpath.equals("webkit/") || assetpath.equals("images/")) return true;
      String[] files = assetManager.list(assetpath);
      if(files == null || files.length == 0) return false;
      for(String filename : files) {
        String srcpath = assetpath + filename;
        String dstpath = outpath + filename;
        // check for directory
        if(!extractAssetPath(assetManager, srcpath + "/", dstpath + "/")) {
          Log.v("Tangram extractAssets", "Copying " + srcpath + " to " + dstpath);
          File dstfile = new File(dstpath);
          if(!replaceAssets && dstfile.exists()) continue;  // don't overwrite existing file
          // this returns InputStream object
          InputStream in = assetManager.open(srcpath);
          // ensure that path exists
          if(!dstfile.getParentFile().isDirectory() && !dstfile.getParentFile().mkdirs()) {
            Log.v("Tangram extractAssets", "mkdirs() failed for " + dstpath);
            continue;
          }
          FileOutputStream out = new FileOutputStream(dstfile);
          // copy byte by byte ... doesn't seem to be a more elegant soln!
          byte[] buf = new byte[65536];
          int len;
          while((len = in.read(buf)) > 0)
            out.write(buf, 0, len);
          out.close();
          in.close();
        }
      }
      return true;
    }
    catch(IOException e) {
      Log.v("Tangram extractAssets", "Error: ", e);
      return false;
    }
  }

  public void openUrl(String url)
  {
    try {
      if("/".equals(url.substring(0,1))) {
        String authority = getApplicationContext().getPackageName() + ".fileprovider";
        File file = new File(url);
        Uri uri = FileProvider.getUriForFile(this, authority, file);
        Intent viewUrlIntent = new Intent(Intent.ACTION_VIEW, uri);
        viewUrlIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(viewUrlIntent);
      }
      else {
        Intent viewUrlIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        startActivity(viewUrlIntent);
      }
    } catch(Exception e) {
      Log.v("Tangram", "Error opening " + url + ": ", e);
    }
  }

  private static final int ID_OPEN_DOCUMENT = 2;
  private static final int ID_READ_FOLDER = 3;
  private static final int ID_WRITE_FOLDER = 3;

  public void openFile()  //Uri pickerInitialUri)
  {
    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
    intent.addCategory(Intent.CATEGORY_OPENABLE);
    intent.setType("*/*");
    //intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, pickerInitialUri);
    startActivityForResult(intent, ID_OPEN_DOCUMENT);
  }

  public void pickFolder(boolean readonly)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(Build.VERSION.SDK_INT >= 33) {
        if(checkSelfPermission(Manifest.permission.READ_MEDIA_IMAGES) != PackageManager.PERMISSION_GRANTED) {
          requestPermissions(new String[]{Manifest.permission.READ_MEDIA_IMAGES,
              Manifest.permission.ACCESS_MEDIA_LOCATION}, PERM_REQ_MEDIA);
          return;
        }
      }
      else if(checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
        requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,
            Manifest.permission.ACCESS_MEDIA_LOCATION}, PERM_REQ_MEDIA);
        return;
      }

      Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
      int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION;
      if(!readonly)
        flags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
      intent.addFlags(flags);
      startActivityForResult(intent, readonly ? ID_READ_FOLDER : ID_WRITE_FOLDER);
    } });
  }

  @Override
  public void onActivityResult(int requestCode, int resultCode, Intent resultData)
  {
    if(requestCode == ID_READ_FOLDER || requestCode == ID_WRITE_FOLDER) {
      if(resultCode == Activity.RESULT_OK && resultData != null) {
        try {
          // ref: https://android.googlesource.com/platform/frameworks/support/+/a9ac247af2afd4115c3eb6d16c05bc92737d6305/documentfile/src/main/java/androidx/documentfile/provider/DocumentFile.java
          Uri treeUri = resultData.getData();
          int flags = requestCode == ID_WRITE_FOLDER ? Intent.FLAG_GRANT_WRITE_URI_PERMISSION : 0;
          getContentResolver().takePersistableUriPermission(treeUri, flags | Intent.FLAG_GRANT_READ_URI_PERMISSION);
          Uri uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, DocumentsContract.getTreeDocumentId(treeUri));
          ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
          MapsLib.openFileDesc(uri.getPath(), pfd.detachFd());  //pfd.getFd());
          //pfd.close();
        } catch(Exception e) {
          Log.v("Tangram", "Error opening directory: " + resultData.getData().toString(), e);
        }
      }
    }
    else if(requestCode == ID_OPEN_DOCUMENT) {
      if(resultCode == Activity.RESULT_OK && resultData != null) {
        if(resultData.getData().toString().startsWith("content://")) {
          try {
            // openFileDescriptor only works for mode="r", but /proc/self/fd/<fd> gives us symlink to actual
            //  file which we can open for writing if we have manage storage permission; w/o manage storage
            //  access is only possible via file description from content resolver; takePersistableUriPermission() does not help
            //getContentResolver().takePersistableUriPermission(resultData.getData(), Intent.FLAG_GRANT_READ_URI_PERMISSION);
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(resultData.getData(), "r");
            MapsLib.openFileDesc(resultData.getData().getPath(), pfd.detachFd());
            //pfd.close();
          } catch(Exception e) {
            Log.v("Tangram", "Error opening document: " + resultData.getData().toString(), e);
          }
        }
        //else
        //  jniOpenFile(intent.getData().getPath());
      }
    }
  }

  public void shareFile(String filepath, String mimetype, String title)
  {
    final String finaltitle = title;
    String authority = getApplicationContext().getPackageName() + ".fileprovider";
    File file = new File(filepath);
    Uri contentUri = FileProvider.getUriForFile(this, authority, file);
    final Intent intent = new Intent(android.content.Intent.ACTION_SEND);
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    intent.putExtra(Intent.EXTRA_SUBJECT, title);
    intent.putExtra(Intent.EXTRA_STREAM, contentUri);
    intent.setType(mimetype);
    startActivity(Intent.createChooser(intent, finaltitle));
  }

  public String getClipboard()
  {
    ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
    if(!clipboard.hasPrimaryClip()) return null;
    ClipData.Item item = clipboard.getPrimaryClip().getItemAt(0);
    return item.coerceToText(this).toString();
  }

  public void setClipboard(String text)
  {
    ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
    ClipData clip = ClipData.newPlainText("simple text", text);
    clipboard.setPrimaryClip(clip);
  }

  // issue is that Views can only be touched from thread that created them, but these methods are called from
  //  drawFrame() on GL thread
  public void showTextInput(int x, int y, int w, int h)
  {
    runOnUiThread(new Runnable() { @Override public void run() { _showTextInput(x,y,w,h); } });
  }

  // ref: SDL SDLActivity.java
  private void _showTextInput(int x, int y, int w, int h)
  {
    ///Log.v("Tangram", "_showTextInput");
    RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(w, h + 15);  //HEIGHT_PADDING);
    params.leftMargin = x;
    params.topMargin = y;
    mTextEdit.setLayoutParams(params);
    mTextEdit.setVisibility(View.VISIBLE);
    mTextEdit.requestFocus();
    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    imm.showSoftInput(mTextEdit, 0);
  }

  public void hideTextInput()
  {
    runOnUiThread(new Runnable() { @Override public void run() { _hideTextInput(); } });
  }

  private void _hideTextInput()
  {
    ///Log.v("Tangram", "_hideTextInput");
    mTextEdit.setLayoutParams(new RelativeLayout.LayoutParams(0, 0));
    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);
  }

  public void setImeText(String text, int selStart, int selEnd)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(mTextEdit.setImeText(text, selStart, selEnd)) {
        ///Log.v("Tangram", "restartInput");
        InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        imm.restartInput(mTextEdit);  // need to clear composing state of keyboard, etc.
      }
    } });
  }

  public void notifyStatusBarBG(boolean isLight)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      //SYSTEM_UI_FLAG_LIGHT_STATUS_BAR requests black text (for light background)
      Window window = getWindow();
      int flags = window.getDecorView().getSystemUiVisibility();
      if(((flags & View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR) != 0) != isLight)
        window.getDecorView().setSystemUiVisibility(flags ^ View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
    } });
  }

  public void sendToBack()
  {
    runOnUiThread(new Runnable() { @Override public void run() { moveTaskToBack(true); } });
  }
}

// Some keyboards (e.g. SwiftKey) edit entire string via InputConnection, while others (e.g. Samsung) only edit
//  composing text via InputConnection and send key events to edit already commited text, so easiest to just
//  use Android EditText to handle everything (old MapsInputConnection removed 12 May 2024)
// refs:
// - https://github.com/sillsdev/chromium-crosswalk/blob/master/content/public/android/java/src/org/chromium/content/browser/input/AdapterInputConnection.java
// - https://android.googlesource.com/platform/frameworks/base.git/+/refs/heads/main/core/java/com/android/internal/inputmethod/EditableInputConnection.java
// - https://github.com/gioui/gio/blob/main/app/GioView.java
class MapsInputConnection extends BaseInputConnection
{
  DummyEdit mEditView;

  public MapsInputConnection(DummyEdit targetView, boolean fullEditor) {
    super(targetView, fullEditor);
    mEditView = targetView;
    ///Log.v("Tangram", "MapsInputConnection ctor " + toString());
  }

  @Override
  public void closeConnection() {
    ///Log.v("Tangram", "closeConnection " + toString());
    super.closeConnection();
  }

  @Override
  public Editable getEditable() {
    return mEditView.getEditable();
  }

  // this must be implemented for SwiftKey keyboard to show suggestions
  @Override
  public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
    Editable editable = getEditable();
    ExtractedText et = new ExtractedText();
    et.text = editable.toString();
    et.partialEndOffset = editable.length();
    et.selectionStart = Selection.getSelectionStart(editable);
    et.selectionEnd = Selection.getSelectionEnd(editable);
    et.flags = ExtractedText.FLAG_SINGLE_LINE;  //mSingleLine ? ExtractedText.FLAG_SINGLE_LINE : 0;
    return et;
  }

  @Override
  public boolean performEditorAction(int actionCode) {
    mEditView.onEditorAction(actionCode);
    return true;
  }

  // override these 4 methods to call updateText() if not using EditText
  //public boolean commitText(CharSequence text, int newCursorPosition) {
  //public boolean setComposingText(CharSequence text, int newCursorPosition) {
  //public boolean setSelection(int start, int end) {

  // deleteSurroundingText() ignores composing text ... can't figure out why this isn't a problem for EditText
  @Override
  public boolean deleteSurroundingText(int beforeLength, int afterLength) {
    finishComposingText();
    return super.deleteSurroundingText(beforeLength, afterLength);
  }
}

class ProxyEdit extends EditText
{
  public boolean enableUpdate = true;

  public ProxyEdit(Context context) {
    super(context);
  }

  @Override
  public void onEditorAction(int actionCode) {
    //super.onEditorAction(actionCode);
    //Log.v("Tangram", "onEditorAction: " + actionCode);
    if(actionCode == EditorInfo.IME_ACTION_DONE) {
      MapsLib.keyEvent(KeyEvent.KEYCODE_ENTER, 1);
      MapsLib.keyEvent(KeyEvent.KEYCODE_ENTER, -1);
    }
  }

  @Override
  protected void onSelectionChanged(int selStart, int selEnd) {
    super.onSelectionChanged(selStart, selEnd);
    updateText();
  }

  @Override
  protected void onTextChanged(CharSequence text, int start, int lengthBefore, int lengthAfter) {
    super.onTextChanged(text, start, lengthBefore, lengthAfter);
    updateText();
  }

  protected boolean updateText() {
    final Editable content = getEditableText();
    if (!enableUpdate || content == null) { return false; }
    ///Log.v("Tangram", "updateText (Java -> C++): " + content.toString() + "; sel: " + Selection.getSelectionStart(content) + ", " + Selection.getSelectionEnd(content));
    MapsLib.imeTextUpdate(content.toString(),
        Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
    return true;
  }
}


class DummyEdit extends View //implements View.OnFocusChangeListener  //, View.OnKeyListener
{
  MapsInputConnection inputConn;
  ProxyEdit mEditText;

  public DummyEdit(Context context)
  {
    super(context);
    setFocusableInTouchMode(true);
    setFocusable(true);
    mEditText = new ProxyEdit(context);
  }

  public Editable getEditable() { return mEditText.getEditableText(); }

  public boolean setImeText(String text, int selStart, int selEnd) {
    ///Log.v("Tangram", "setImeText (C++ -> Java): " + text + " sel: " + selStart + ", " + selEnd + " was: " + mEditText.getText());
    mEditText.enableUpdate = false;
    try {  // prevent crash if text editing state gets messed up
      mEditText.setText(text);
      mEditText.setSelection(selStart, selEnd);
    } catch(Exception e) {
      ///Log.v("Tangram setImeText", "Error: ", e);
    }
    mEditText.enableUpdate = true;
    return isFocused();
  }

  @Override
  protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
    ///Log.v("Tangram", "Focus change: " + (gainFocus ? "true" : "false"));
    super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
    if(!gainFocus)
      MapsLib.keyEvent(-1, -1);  // keyboard hidden
  }

  @Override
  public boolean onCheckIsTextEditor() { return true; }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event) { return mEditText.onKeyDown(keyCode, event); }

  @Override
  public boolean onKeyUp(int keyCode, KeyEvent event) { return mEditText.onKeyUp(keyCode, event); }

  //@Override
  public void onEditorAction(int actionCode) { mEditText.onEditorAction(actionCode); }

  @Override
  public boolean onKeyPreIme(int keyCode, KeyEvent event)
  {
    if(event.getAction() == KeyEvent.ACTION_UP && keyCode == KeyEvent.KEYCODE_BACK) {
      clearFocus();  //MapsLib.keyEvent(-1, -1);  // keyboard hidden
    }
    if(keyCode == KeyEvent.KEYCODE_BACK) {
      MapsLib.keyEvent(keyCode, event.getAction() == KeyEvent.ACTION_UP ? -1 : 1);
      return true;  // swallow key
    }
    return super.onKeyPreIme(keyCode, event);
  }

  @Override
  public InputConnection onCreateInputConnection(EditorInfo outAttrs)
  {
    inputConn = new MapsInputConnection(this, true);
    outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES | InputType.TYPE_TEXT_FLAG_AUTO_CORRECT;  //TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
    outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_ACTION_DONE;
    return inputConn;
  }
}
