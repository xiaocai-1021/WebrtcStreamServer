#pragma once

#include <boost/optional.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#include "media_source.h"
#include "nlohmann/json.hpp"
#include "random.h"

/**
 * @brief Manage all media sources.
 *
 */
class MediaSourceManager {
 public:
  static MediaSourceManager& GetInstance();

  /**
   * @brief Add and start pull stream.
   *
   * @param Url of stream.
   * @return ID of stream.
   */
  boost::optional<std::string> Add(const std::string& url);

  /**
   * @brief Remove and stop pull stream.
   *
   * @param ID of stream.
   */
  void Remove(const std::string& id);

  /**
   * @brief Query media source with ID.
   *
   * @param id
   * @return std::shared_ptr<MediaSource>
   */
  std::shared_ptr<MediaSource> Query(const std::string& id);

  /**
   * @brief List all media sources.
   *
   * @return JSON array describing all media sources.
   */
  nlohmann::json List();

  /**
   * @brief Stop all media sources.
   *
   */
  void StopAll();

 private:
  MediaSourceManager() = default;
  std::unordered_map<std::string, std::shared_ptr<MediaSource>> database_;
  Random random_;
};