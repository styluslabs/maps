#pragma once

#include "mapscomponent.h"

struct OfflineMapInfo;

class MapsOffline : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  ~MapsOffline();
  int numOfflinePending() const;
  void saveOfflineMap(int mapid, Tangram::LngLat lngLat00, Tangram::LngLat lngLat11, int maxZoom);
  void updateProgress();
  void downloadCompleted(int id, bool canceled);
  void resumeDownloads();
  Widget* createPanel();

  static void queueOfflineTask(int mapid, std::function<void()>&& fn);
  static int64_t shrinkCache(int64_t maxbytes);
  static void runSQL(std::string dbpath, std::string sql);

  Widget* offlinePanel = NULL;

private:
  MarkerID rectMarker = 0;
  Widget* offlineContent = NULL;

  void openForImport(std::string srcpath);
  bool importFile(std::string destsrc, std::string srcpath, OfflineMapInfo olinfo, bool hasPois);
  void populateOffline();
  bool cancelDownload(int mapid);
  std::unique_ptr<SelectDialog> selectDestDialog;
  std::unique_ptr<Dialog> downloadDialog;
};
