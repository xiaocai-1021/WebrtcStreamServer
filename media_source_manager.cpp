#include "media_source_manager.h"

#include <algorithm>

MediaSourceManager& MediaSourceManager::GetInstance() {
  static MediaSourceManager manager;
  return manager;
}

boost::optional<std::string> MediaSourceManager::Add(const std::string& url) {
  auto result = std::find_if(
      database_.begin(), database_.end(),
      [&url](std::pair<std::string, std::shared_ptr<MediaSource>> e) {
        return e.second->Url() == url;
      });
  if (result != database_.end())
    return result->first;

  auto media_source = std::make_shared<MediaSource>();
  if (!media_source->Open(url))
    return boost::none;
  media_source->Start();

  std::string id = random_.RandomString(32);
  database_[id] = media_source;
  return id;
}

nlohmann::json MediaSourceManager::List() {
  nlohmann::json json = nlohmann::json::array();
  for (auto& i : database_) {
    nlohmann::json streamItem;
    streamItem["id"] = i.first;
    streamItem["url"] = i.second->Url();
    json.push_back(streamItem);
  }
  return json;
}

void MediaSourceManager::StopAll() {
  for (auto& i : database_) {
    i.second->Stop();
  }
}

void MediaSourceManager::Remove(const std::string& id) {
  auto result = database_.find(id);
  if (result != database_.end()) {
    (*result).second->Stop();
    database_.erase(result);
  }
}

std::shared_ptr<MediaSource> MediaSourceManager::Query(const std::string& id) {
  auto result = database_.find(id);
  return (result == database_.end()) ? nullptr : (*result).second;
}