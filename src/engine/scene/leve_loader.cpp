#include "leve_loader.h"
#include "../object/game_object.h"
#include "../component/transform_component.h"
#include "../component/parallax_component.h"
#include "../component/tilelayer_component.h"
#include "../component/sprite_component.h"
#include "../scene/scene.h"
#include "../core/context.h"
#include <fstream>
#include <spdlog/spdlog.h>
#include <glm/vec2.hpp>
namespace engine::scene
{

    /** 加载关卡文件 */
    bool LevelLoader::loadLevel(const std::string &level_path, Scene *scene)
    {
        std::ifstream file(level_path);
        if (!file.is_open())
        {
            spdlog::error("无法打开关卡文件，{}", level_path);
            return false;
        }
        // 关卡文件 json
        nlohmann::json json_data;
        try
        {
            file >> json_data;
        }
        catch (const nlohmann::json::parse_error &e)
        {
            spdlog::error("解析关卡 json 文件失败，error: {}", e.what());
            return false;
        }
        // 判断是否存在以及是否是数组类型
        _map_path = level_path;
        _map_size = glm::ivec2(json_data.value("width", 0), json_data.value("height", 0));
        _tile_size = glm::ivec2(json_data.value("tilewidth", 0), json_data.value("tileheight", 0));
        // 加载 tileset 数据
        if (!json_data.contains("tilesets") || !json_data["tilesets"].is_array())
        {
            spdlog::error("地图：{} 中缺少 tilesets 层，或者非数组形式", level_path);
            return false;
        }
        for (const auto &tileset_json : json_data["tilesets"])
        {
            if (!tileset_json.contains("source") || !tileset_json["source"].is_string() ||
                !tileset_json.contains("firstgid") || !tileset_json["firstgid"].is_number_integer())
            {
                spdlog::error("tileset 缺少 source 或 firstgid 属性, 或者属性类型不正确");
            }
            auto tileset_path = resolvePath(tileset_json.value("source", _map_path), _map_path);
            auto first_gid = tileset_json.value("firstgid", 0);
            loadTileset(tileset_path, first_gid);
        }
        // 加载数据层文件 Layers
        if (!json_data.contains("layers") || !json_data["layers"].is_array())
        {
            spdlog::error("地图：{} 中缺少 Layers 层，或者非数组形式", level_path);
            return false;
        }
        for (const auto &layer_json : json_data["layers"])
        {
            // 获取各个图层对象中类型（type）字段
            std::string layer_type = layer_json.value("type", "none");
            // 对象是否可见
            if (!layer_json.value("visible", true))
            {
                spdlog::info("图层 '{}' 不可见，跳过加载", layer_json.value("name", "Unnane"));
                continue;
            }
            // 根据图层类型决定加载方法
            if (layer_type == "imagelayer")
            {
                loadImageLayer(layer_json, scene);
            }
            else if (layer_type == "tilelayer")
            {
                loadTileLayer(layer_json, scene);
            }
            else if (layer_type == "objectgroup")
            {
                loadObjectLayer(layer_json, scene);
            }
            else
            {
                spdlog::warn("不支持的图层类型: {}", layer_type);
            }
        }
        spdlog::info("关卡文件加载完成,{}", level_path);
        return true;
    }

    void LevelLoader::loadImageLayer(const nlohmann::json &layer_json, Scene *scene)
    {
        // 获取图层名称
        const std::string &layer_name = layer_json.value("name", "Unnamed");
        // 获取纹路路径
        const std::string &image_path = layer_json.value("image", "");
        if (image_path.empty())
        {
            spdlog::error("图层:{}, 缺少 image 属性", layer_name);
            return;
        }
        auto texture_id = resolvePath(image_path, _map_path);
        // 获取图层偏移量
        const glm::vec2 offset = glm::vec2(layer_json.value("offsetx", 0.0f), layer_json.value("offsety", 0.0f));
        // 获取视差因子及重复标志
        const glm::vec2 scroll_factor = glm::vec2(layer_json.value("parallaxx", 1.0f), layer_json.value("parallaxy", 1.0f));
        const glm::bvec2 repeat = glm::bvec2(layer_json.value("repeatx", true), layer_json.value("repeaty", true));
        // 创建游戏对象
        auto game_object = std::make_unique<engine::object::GameObject>(*engine::core::Context::Current, layer_name);
        // 依次添加 Transform 与 Parallax 组件
        game_object->addComponent<engine::component::TransformComponent>(offset);
        game_object->addComponent<engine::component::ParallaxComponent>(texture_id, scroll_factor, repeat);
        // 将对象添加到场景中
        scene->addGameObject(std::move(game_object));
        spdlog::info("加载图层：{} 完成", layer_name);
    }
    void LevelLoader::loadTileLayer(const nlohmann::json &layer_json, Scene *scene)
    {
        if (!layer_json.contains("data") || !layer_json["data"].is_array())
        {
            spdlog::error("图层：{} 缺少 data 属性，或者非数组类型", layer_json.value("name", "Unnamed"));
            return;
        }
        // 准备 TileInfo Vector （瓦片数量 = 地图宽 * 地图高）
        std::vector<engine::component::TileInfo> tiles;
        tiles.reserve(_map_size.x * _map_size.y);

        // 获取图层数据
        const auto &data = layer_json["data"];
        // 根据 GID 获取必要信息,并依次填充 TileInfo Vector
        for (const auto &gid : data)
        {
            tiles.push_back(getTileInfoByGid(gid));
        }

        // 获取图层名称
        const std::string &layer_name = layer_json.value("name", "Unnamed");
        // 创建游戏对象
        auto game_object = std::make_unique<engine::object::GameObject>(*engine::core::Context::Current, layer_name);
        // 添加 TilelayerComponent 组件
        game_object->addComponent<engine::component::TilelayerComponent>(_tile_size, _map_size, std::move(tiles));
        // 将对象添加到场景中
        scene->addGameObject(std::move(game_object));
    }
    void LevelLoader::loadObjectLayer(const nlohmann::json &layer_json, Scene *scene)
    {
        if(!layer_json.contains("objects") || !layer_json["objects"].is_array())
        {
            spdlog::error("图层：{} 缺少 objects 属性，或者非数组类型", layer_json.value("name", "Unnamed"));
            return;
        }
        // 获取对象数据
        for (const auto &object_json : layer_json["objects"])
        {
            // 获取数据对象gid
            int gid = object_json.value("gid", 0);
            if (gid == 0)
            {
                // 如果为0代表不存在，则代表绘制的形状为碰撞盒、触发器等没有图片显示的东西
            }
            else
            {
                //对象gid存在，按照图片解析逻辑
                auto tile_info = getTileInfoByGid(gid);
                if (tile_info.sprite.getTextureId().empty())
                {
                    spdlog::error("对象 gid: {} 缺少 texture_id", gid);
                    continue;
                }
                // 获取对象位置
                glm::vec2 position = glm::vec2(object_json.value("x", 0.0f), object_json.value("y", 0.0f));
                // 获取对象大小
                const glm::vec2 dst_size = glm::vec2(object_json.value("width", 0.0f), object_json.value("height", 0.0f));
                position = glm::vec2(position.x, position.y + dst_size.y); // 左下角到左下角

                                // 获取对象旋转角度
                auto rotation = object_json.value("rotation", 0.0f);
                auto src_size_opt = tile_info.sprite.getSourceRect();
                if (!src_size_opt)
                {
                    spdlog::error("对象 gid: {} 缺少 矩形数据（SourceRect）", gid);
                    continue;
                }
                auto src_size = glm::vec2(src_size_opt->size.x, src_size_opt->size.y);
                auto scale = dst_size / src_size;
                // 获取对象名称
                const std::string &object_name = object_json.value("name", "Unnamed");
               
                // 创建游戏对象
                auto game_object = std::make_unique<engine::object::GameObject>(*engine::core::Context::Current, object_name);
                // 添加 Transform 组件
                game_object->addComponent<engine::component::TransformComponent>(position, scale, rotation);
                // 添加 Sprite 组件
                game_object->addComponent<engine::component::SpriteComponent>(std::move(tile_info.sprite));
                // 将对象添加到场景中
                scene->addGameObject(std::move(game_object));
                spdlog::info("加载对象：{} 完成", object_name);
            }
        }
    }
    void LevelLoader::loadTileset(const std::string_view tileset_path, int first_gid)
    {
        // 调试：打印到底在尝试打开什么路径
        spdlog::debug("正在加载 Tileset: {}, GID: {}", tileset_path, first_gid);
        std::ifstream tileset_file(tileset_path.data());
        if (!tileset_file.is_open())
        {
            spdlog::error("无法打开 Tileset 文件: {}", tileset_path);
            return;
        }

        nlohmann::json tileset_data;
        try
        {
            tileset_file >> tileset_data;
        }
        catch (const nlohmann::json::parse_error &e)
        {
            spdlog::error("解析失败: {}", e.what());
            return;
        }

        // 必须确保这两个字段存在，因为 getTileInfoByGid 强依赖它们
        tileset_data["firstgid"] = first_gid;
        tileset_data["file_path"] = std::string(tileset_path);

        _tileset_data[first_gid] = std::move(tileset_data);
    }
    engine::component::TileInfo LevelLoader::getTileInfoByGid(int gid) const
    {
        if (gid == 0)
        {
            return engine::component::TileInfo();
        }
        // uppper_bound 返回第一个大于等于 gid 的键
        auto tileset_it = _tileset_data.upper_bound(gid);
        if (tileset_it == _tileset_data.begin())
        {
            spdlog::error("无法找到 GID: {} 对应的 Tileset", gid);
            return engine::component::TileInfo();
        }
        --tileset_it;
        const auto &tileset = tileset_it->second;
        auto local_id = gid - tileset["firstgid"].get<int>();
        const std::string file_path = tileset.value("file_path", "");
        if (file_path.empty())
        {
            spdlog::error("Tileset 文件 {} 缺少 file_path 属性", file_path);
            return engine::component::TileInfo();
        }
        // 图块集分为两种 类型：图块集和图块集图像
        if (tileset.contains("image")) // 单体图片
        {
            // 获取图片路径
            const std::string texture_id = resolvePath(tileset["image"].get<std::string>(), file_path);
            // 获取图块在图片中的坐标
            const auto coordinate_x = local_id % tileset["columns"].get<int>();
            const auto coordinate_y = local_id / tileset["columns"].get<int>();
            // 根据坐标确定源矩形
            engine::utils::FRect source_rect{
                glm::vec2{coordinate_x * _tile_size.x, coordinate_y * _tile_size.y},
                glm::vec2{_tile_size.x, _tile_size.y}};
            engine::render::Sprite sprite{texture_id, source_rect};
            return engine::component::TileInfo(std::move(sprite), engine::component::TileType::Normal);
        }
        else // 多图片逻辑
        {
            if (!tileset.contains("tiles"))
            {
                spdlog::error("Tileset 文件 {} 缺少 tiles 属性", tileset_it->first);
                return engine::component::TileInfo();
            }
            // 遍历 _tiles 数组， 根据id查找对应的瓦片
            const auto &tiles_json = tileset["tiles"];
            for (const auto &tile_json : tiles_json)
            {
                auto tile_id = tile_json.value("id", 0);
                if (tile_id == local_id)
                {
                    if (!tile_json.contains("image"))
                    {
                        spdlog::error("Tileset 文件 {} 中 瓦片 {} 缺少 image 属性", tileset_it->first, tile_id);
                        return engine::component::TileInfo();
                    }
                    // 获取图片路径
                    const std::string texture_id = resolvePath(tile_json["image"].get<std::string>(), file_path);
                    // 确认图片尺寸
                    // 修正：从 tile_json 中安全获取宽高，默认偏移为 0
                    float w = tile_json.value("imagewidth", 0.0f);
                    float h = tile_json.value("imageheight", 0.0f);
                    // 从 json 中获取源矩形
                    engine::utils::FRect source_rect{
                        glm::vec2{0.0f, 0.0f}, // 集合图块通常引用整张图
                        glm::vec2{w, h}};
                    engine::render::Sprite sprite{texture_id, source_rect};
                    return engine::component::TileInfo(std::move(sprite), engine::component::TileType::Normal);
                }
            }
        }
        spdlog::error("图块集 {} 无法找到 GID: {} 对应的 瓦片", tileset_it->first, gid);
        return engine::component::TileInfo();
    }
    std::string LevelLoader::resolvePath(std::string_view image_path, const std::string_view file_path) const
    {
        namespace fs = std::filesystem;

        // 1. 将输入转换为路径对象（移出 try 块，解决作用域问题）
        fs::path img_p(image_path);

        // 2. 快速路径：如果是绝对路径，直接规范化并返回
        if (img_p.is_absolute())
        {
            return img_p.lexically_normal().string();
        }

        try
        {
            // 3. 这里的 _map_path 建议预先存为 fs::path 类型以提升性能
            fs::path file_dir = fs::path(file_path).parent_path();

            /* * 4. 使用 lexically_normal() 代替 canonical()
             * - lexically_normal: 仅逻辑处理 "../" 和 "./"，不要求文件必须存在。
             * - canonical: 要求文件必须存在，否则抛出异常（过于严格）。
             */
            return (file_dir / img_p).lexically_normal().string();
        }
        catch (const fs::filesystem_error &e)
        {
            // 记录更详细的错误信息
            spdlog::error("路径拼接异常 | 输入: {} | 错误: {}", image_path, e.what());
            return std::string(image_path);
        }
    }
}