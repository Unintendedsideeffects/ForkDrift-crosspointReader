#pragma once

#include <memory>
#include <string>

class Activity;

class ActivityManager {
 public:
  void requestUpdate(bool immediate = false);
  void requestUpdateAndWait();
  void pushActivity(std::unique_ptr<Activity> activity);
  void popActivity();
  void goHome();
  void goToRecentBooks();
  void goToReader(std::string path, bool suppressBackRelease = false);
};

extern ActivityManager activityManager;
