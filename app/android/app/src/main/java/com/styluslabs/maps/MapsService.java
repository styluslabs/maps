package com.styluslabs.maps;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import android.location.Location;
import android.location.LocationManager;
import android.location.LocationListener;
import android.app.Notification.Builder;
//import androidx.core.app.NotificationCompat;
//import com.styluslabs.maps.R;


public class MapsService extends Service implements LocationListener
{
  public static final String START_RECORDING = "START";
  public static final String STOP_RECORDING = "STOP";
  public static final String EXTRA_INTERVAL = "EXTRA_INTERVAL";
  public static final String EXTRA_DISTANCE = "EXTRA_DISTANCE";

  public static final int NOTIFICATION_ID = 1;
  public static final String CHANNEL_ID = "MapsService_channel";
  //private boolean isRecording = false;
  private LocationManager locationManager;

  @Override
  public void onCreate()
  {
    super.onCreate();
    locationManager = (LocationManager) getSystemService(LOCATION_SERVICE);
  }

  // GPS left on if interval < 10 sec - see Android GnssLocationProvider.java : GPS_POLLING_THRESHOLD_INTERVAL
  private void startLocationUpdates(float minInterval, float minDistance)
  {
    locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, (long)(minInterval*1000), minDistance, this);
    if(locationManager.getProvider("fused") != null) {
      locationManager.requestLocationUpdates("fused", minInterval, minDistance, this);
      onLocationChanged(locationManager.getLastKnownLocation("fused"));
    }
    else
      onLocationChanged(locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER));
  }

  @Override
  public void onLocationChanged(Location loc)
  {
    if(loc == null) return;  // getLastKnownLocation() can return null
    float poserr = loc.getAccuracy();  // accuracy in meters
    double alt = loc.getAltitude();  // meters
    float dir = loc.getBearing();  // bearing (direction of travel) in degrees
    float direrr = loc.getBearingAccuracyDegrees();
    double lat = loc.getLatitude();  // degrees
    double lng = loc.getLongitude();  // degrees
    float spd = loc.getSpeed();  // m/s
    float spderr = loc.getSpeedAccuracyMetersPerSecond();  // speed accuracy in m/s
    long time = loc.getTime();  // ms since unix epoch
    float alterr = loc.getVerticalAccuracyMeters();  // altitude accuracy in meters
    MapsLib.updateLocation(time, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr);
  }

  @Override
  public void onLowMemory() {
    MapsLib.onLowMemory();
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int startId)
  {
    String action = intent.getAction();
    if(action.equals(START_RECORDING)) {
      prepareNotification();  // this calls startForeground()
      startLocationUpdates(intent.getFloatExtra(EXTRA_INTERVAL), intent.getFloatExtra(EXTRA_DISTANCE));
    }
    else if(action.equals(STOP_RECORDING)) {
      //isRecording = false;
      stopForeground(true);
      stopSelf();  //stopSelfResult(startId);
    }
    return START_STICKY;
  }

  private void prepareNotification()
  {
    if(Build.VERSION.SDK_INT >= 26) {
      NotificationChannel serviceChannel = new NotificationChannel(
          CHANNEL_ID, "Location Service Channel", NotificationManager.IMPORTANCE_DEFAULT);
      getSystemService(NotificationManager.class).createNotificationChannel(serviceChannel);
    }
    Intent intent = new Intent(this, MapsActivity.class);
    intent.setAction(Intent.ACTION_MAIN);
    intent.addCategory(Intent.CATEGORY_LAUNCHER);
    PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, 0);

    Notification notification = new Notification.Builder(this, CHANNEL_ID)  //NotificationCompat
        .setContentTitle("Explore")
        .setContentText("Explore is recording a track")
        .setSmallIcon(R.mipmap.ic_launcher)
        .setContentIntent(pendingIntent)
        .build();
    startForeground(NOTIFICATION_ID, notification);  //ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
  }

  //@Nullable
  @Override
  public IBinder onBind(Intent intent) { return null; }
}
