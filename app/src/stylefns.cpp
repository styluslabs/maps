#include "scene/scene.h"
#include "log.h"
#include "ulib/stringutil.h"

namespace Tangram {

static std::unordered_map<std::string, std::string> yamlToMap(const YAML::Node& yml)
{
  std::unordered_map<std::string, std::string> res;
  for(auto kv : yml.pairs()) {
    res.emplace(kv.first.Scalar(), kv.second.Scalar());
  }
  return res;
}

struct NativeStyleCtx
{
  std::function<std::string(const Feature& feature)> poi_sprite_fn;
};

static NativeStyleCtx* getContext(Scene& scene)
{
  auto* ctx = static_cast<NativeStyleCtx*>(scene.nativeContext().get());
  if(ctx) { return ctx; }

  auto spctx = std::make_shared<NativeStyleCtx>();
  scene.nativeContext() = spctx;
  ctx = spctx.get();

  struct TagIcons { std::string tag; std::unordered_map<std::string, std::string> valToSprite; };
  std::vector<TagIcons> poi_icons;

  const YAML::Node& poi_icons_yml = scene.config()["global"]["poi_icons"];
  // we rely on the fact that gaml preserves order of yaml maps
  for(auto group : poi_icons_yml.pairs()) {
    poi_icons.push_back({group.first.Scalar(), yamlToMap(group.second)});
  }

  ctx->poi_sprite_fn = [poi_icons = std::move(poi_icons)](const Feature& feature) {
    for(const TagIcons& group : poi_icons) {
      auto prop = feature.props.get(group.tag);
      if(prop.is<std::string>()) {
        auto tagval = prop.get<std::string>();
        auto it = group.valToSprite.find(tagval);
        if(it != group.valToSprite.end()) {
          return it->second;
        }
      }
    }
    auto shopprop = feature.props.get("shop");
    return shopprop.is<std::string>() ? "shop" : std::string();
  };

  return ctx;
}

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
    return [
      show_name_en = scene.config()["global"]["show_name_en"].as<bool>(true),
      abbrev_map = yamlToMap(scene.config()["global"]["road_name_abbrev"])
    ](const Feature& feature, StyleParam::Value& val) {
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

  if(tag == "poi_sprite") {
    return [ctx = getContext(scene)](const Feature& feature, StyleParam::Value& val) {
      val = ctx->poi_sprite_fn(feature);
      return true;
    };
  }

  if(tag == "poi_color") {
    return [
      ctx = getContext(scene),
      poi_type = yamlToMap(scene.config()["global"]["poi_type"]),
      poi_color = yamlToMap(scene.config()["global"]["poi_color"])
    ](const Feature& feature, StyleParam::Value& val) {
      auto sprite = ctx->poi_sprite_fn(feature);
      auto typeit = poi_type.find(sprite);
      auto colorit = typeit != poi_type.end() ? poi_color.find(typeit->second) : poi_color.end();
      std::string colorstr = colorit != poi_color.end() ? colorit->second : poi_color.at("generic");
      Color color(0, 0, 0, 255);
      if(!StyleParam::parseColor(colorstr, color)) {
        LOGW("Error parsing color %s", colorstr.c_str());
      }
      val = color.abgr;
      return true;
    };
  }

  return {};
}

}  // namespace Tangram
