#pragma once

#include "mapscomponent.h"

struct OfflineMapInfo;
class PlatformFile;

class MapsOffline : public MapsComponent
{
public:
  MapsOffline(MapsApp* _app);
  ~MapsOffline();
  int numOfflinePending() const;
  void saveOfflineMap(int mapid, Tangram::LngLat lngLat00, Tangram::LngLat lngLat11, int maxZoom);
  void updateProgress(int mapid, const std::string& msg);
  void downloadCompleted(int id, bool canceled, int64_t size);
  void resumeDownloads();
  void openForImport(std::unique_ptr<PlatformFile> srcfile);
  void populateOffline();
  Widget* createPanel();

  static void queueOfflineTask(int mapid, std::function<void()>&& fn);
  static void shrinkCache(int64_t maxbytes);
  static void onCacheGrowth(int64_t bytes);
  static bool runSQL(std::string dbpath, std::string sql);
  static void clearCacheFile(std::string path);

  Widget* offlinePanel = NULL;

private:
  MarkerID rectMarker = 0;
  Widget* offlineContent = NULL;

  bool importFile(std::string destsrc, std::unique_ptr<PlatformFile> srcfile, OfflineMapInfo olinfo, bool hasPois);
  bool cancelDownload(int mapid);
  std::unique_ptr<SelectDialog> selectDestDialog;
  std::unique_ptr<Dialog> downloadDialog;
};
