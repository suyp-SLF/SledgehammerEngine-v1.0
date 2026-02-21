#pragma once
#include <string>
#include <string_view>
#include <nlohmann/json_fwd.hpp>
namespace engine::scene
{
    class Scene;
    class LevelLoader final
    {
        std::string _map_path;
    public:
        LevelLoader() = default;
        bool loadLevel(const std::string& level_path, Scene* scene);
    private:
        void loadImageLayer(const nlohmann::json& layer_json, Scene* scene);
        void loadTileLayer(const nlohmann::json& layer_json, Scene* scene);
        void loadObjectLayer(const nlohmann::json& layer_json, Scene* scene);

        std::string resolvePath(std::string_view image_path);
    };
}