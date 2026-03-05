#pragma once
#include <string>
#include <string_view>
#include <glm/vec2.hpp>
#include <nlohmann/json.hpp>
namespace engine::render
{
    class Sprite;
}
namespace engine::component
{
    class TileInfo;
}
namespace engine::scene
{
    class Scene;
    class LevelLoader final
    {
        std::string _map_path;
        glm::ivec2 _map_size;
        glm::ivec2 _tile_size;
        std::map<int, nlohmann::json> _tileset_data;
    public:
        LevelLoader() = default;
        bool loadLevel(const std::string& level_path, Scene* scene);
    private:
        void loadImageLayer(const nlohmann::json& layer_json, Scene* scene);
        void loadTileLayer(const nlohmann::json& layer_json, Scene* scene);
        void loadObjectLayer(const nlohmann::json& layer_json, Scene* scene);
        void loadTileset(const std::string_view tileset_path, int first_gid);
        engine::component::TileInfo getTileInfoByGid(int gid) const;
        engine::render::Sprite getSpriteByGid(int gid) const;

        std::string resolvePath(const std::string_view image_path, const std::string_view file_path) const;
    };
}