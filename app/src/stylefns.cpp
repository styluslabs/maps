#include "scene/scene.h"
#include "ulib/stringutil.h"

namespace Tangram {

// Special comments of the form /*@FunctionTag*/ (no spaces!) can be included in scene file Javascript
//  functions to allow them to be replaced by native functions for performance; of course, JS source should
//  be kept up-to-date with the native function here for documentation purposes
NativeStyleFn userGetStyleFunction(Scene& scene, const std::string& jsSource)
{
  size_t prefix = jsSource.find("/*@");
  if(prefix == std::string::npos) { return {}; }
  prefix += 3;
  size_t suffix = jsSource.find("*/", prefix);
  if(suffix == std::string::npos) { return {}; }
  std::string tag = jsSource.substr(prefix, suffix - prefix);

  // obviously if we have many fns, we can dispatch differently (or maybe compiler will optimize if/else)
  if(tag == "road_name_abbrev") {
    bool show_name_en = scene.config()["global"]["show_name_en"].as<bool>(true);
    const YAML::Node& abbrevs = scene.config()["global"]["road_name_abbrev"];
    std::unordered_map<std::string, std::string> abbrev_map;
    for(auto kv : abbrevs.pairs()) {
      abbrev_map.emplace(kv.first.Scalar(), kv.second.Scalar());
    }

    return [=, abbrev_map = std::move(abbrev_map)](const Feature& feature, StyleParam::Value& val) {
      std::string name;
      if(show_name_en) {
        auto prop = feature.props.get("name_en");
        if(prop.is<std::string>()) { name = prop.get<std::string>(); }
      }
      if(name.empty()) {
        auto prop = feature.props.get("name");
        if(prop.is<std::string>()) { name = prop.get<std::string>(); }
      }
      if(!name.empty()) {
        bool replaced = false;
        auto parts = splitStr<std::vector>(name, " ");
        for(auto& part : parts) {
          auto it = abbrev_map.find(part);
          if(it != abbrev_map.end()) { part = it->second; replaced = true; }
        }
        if(replaced) { name = joinStr(parts, " "); }
      }
      val = name;
      return true;
    };
  }
  return {};
}

}
