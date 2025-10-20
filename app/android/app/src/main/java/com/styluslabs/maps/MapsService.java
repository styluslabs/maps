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
    long intervalMsec = (long)(minInterval*1000);
    locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, intervalMsec, minDistance, this);
    if(locationManager.getProvider("fused") != null) {
      locationManager.requestLocationUpdates("fused", intervalMsec, minDistance, this);
    //  onLocationChanged(locationManager.getLastKnownLocation("fused"));
    }
    //else
      onLocationChanged(locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER));
  }

  @Override
  public void onLocationChanged(Location loc)
  {
    if(loc == null) return;  // getLastKnownLocation() can return null
    MapsActivity.updateLocation(loc, true);
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
      startLocationUpdates(intent.getFloatExtra(EXTRA_INTERVAL, 0), intent.getFloatExtra(EXTRA_DISTANCE, 0));
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
      serviceChannel.setSound(null, null);
      getSystemService(NotificationManager.class).createNotificationChannel(serviceChannel);
    }
    Intent intent = new Intent(this, MapsActivity.class);
    intent.setAction(Intent.ACTION_MAIN);
    intent.addCategory(Intent.CATEGORY_LAUNCHER);
    PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_IMMUTABLE);

    Notification notification = new Notification.Builder(this, CHANNEL_ID)  //NotificationCompat
        .setContentTitle("Ascend")
        .setContentText("Ascend Maps is recording a track")
        .setSmallIcon(R.mipmap.ic_launcher)
        .setOngoing(true)  // prevent dismissal (at least until Android 14)
        .setContentIntent(pendingIntent)
        .build();
    startForeground(NOTIFICATION_ID, notification);  //ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
  }

  //@Nullable
  @Override
  public IBinder onBind(Intent intent) { return null; }
}
