#include "level_loader.h"
#include "../object/game_object.h"
#include "../component/transform_component.h"
#include "../component/parallax_component.h"
#include "../component/tilelayer_component.h"
#include "../component/sprite_component.h"
#include "../scene/scene.h"
#include "../core/context.h"
#include "../resource/resource_manager.h"
#include "../resource/texture_manager.h"
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
        std::string image_path = layer_json.value("image", "");
        if (image_path.empty())
            return;
        spdlog::debug("开始 加载图像层: {}", layer_json.value("name", "Unnamed"));
        // 1. 解析路径并创建 GameObject
        std::string texture_id = resolvePath(image_path, _map_path);
        auto go = std::make_unique<engine::object::GameObject>(*engine::core::Context::Current, layer_json.value("name", "ImageLayer"));

        // 2. 获取偏移位置
        float x = layer_json.value("offsetx", 0.0f);
        float y = layer_json.value("offsety", 0.0f);

        go->addComponent<engine::component::TransformComponent>(glm::vec2(x, y));

        // 3. 创建 SpriteComponent (关键点)
        // 注意：这里需要确保构造函数能处理全图 UV
        go->addComponent<engine::component::ParallaxComponent>(texture_id);

        // 4. 手动触发资源就绪检查，或者确保它能被自动更新
        // 如果你没在构造函数里设置默认 UV，这里要手动设一下
        // 假设你的 SpriteComponent 现在默认就是归一化 (0,0,1,1)

        // 5. 设置透明度（Image Layer 通常支持 opacity）
        float opacity = layer_json.value("opacity", 1.0f);
        // 如果你的 SpriteComponent 有设置颜色的功能：
        // sprite_comp->setColor(glm::vec4(1, 1, 1, opacity));
        // ⚡️ 必须添加这一行，把所有权移交给 Scene
        scene->addGameObject(std::move(go));
        spdlog::debug("完成 加载图像层: {}, 纹理: {}, 位置: ({}, {})",
                      layer_json["name"].get<std::string>(), texture_id, x, y);
    }
    void LevelLoader::loadTileLayer(const nlohmann::json &layer_json, Scene *scene)
    {
        if (!layer_json.contains("data") || !layer_json["data"].is_array())
        {
            spdlog::error("图层：{} 缺少 data 属性，或者非数组类型", layer_json.value("name", "Unnamed"));
            return;
        }
        std::vector<engine::component::TileData> tiles;
        std::string main_texture_id = "";

        for (const auto &gid_raw : layer_json["data"])
        {
            uint32_t gid = gid_raw.get<uint32_t>() & ~(0x80000000 | 0x40000000 | 0x20000000);

            if (gid == 0)
            {
                tiles.push_back(engine::component::TileData()); // 默认构造函数应设置 type = Empty
            }
            else
            {
                auto info = getTileDataByGid(gid);
                tiles.push_back(info);

                // 自动拾取该层的第一个有效纹理作为层纹理
                if (main_texture_id.empty())
                {
                    main_texture_id = getSpriteByGid(gid).getTextureId();
                }
            }
        }

        auto game_object = std::make_unique<engine::object::GameObject>(*engine::core::Context::Current, layer_json.value("name", "TileLayer"));
        auto *comp = game_object->addComponent<engine::component::TilelayerComponent>(_tile_size, _map_size, std::move(tiles));

        // 记录偏移（Tiled 中的 offsetx/y）
        float ox = layer_json.value("offsetx", 0.0f);
        float oy = layer_json.value("offsety", 0.0f);
        game_object->addComponent<engine::component::TransformComponent>(glm::vec2(ox, oy));

        scene->addGameObject(std::move(game_object));
    }
    void LevelLoader::loadObjectLayer(const nlohmann::json &layer_json, Scene *scene)
    {
        if (!layer_json.contains("objects"))
            return;

        for (const auto &object_json : layer_json["objects"])
        {
            uint32_t raw_gid = object_json.value("gid", 0u);
            if (raw_gid == 0)
                continue;

            // 1. 处理 GID
            int gid = raw_gid & ~(0x80000000 | 0x40000000 | 0x20000000);
            auto sprite = getSpriteByGid(gid);

            if (sprite.getTextureId().empty())
            {
                spdlog::warn("物体 GID {} 找不到对应纹理，跳过", gid);
                continue;
            }

            // 2. 获取 Tiled 属性
            float x = object_json.value("x", 0.0f);
            float y = object_json.value("y", 0.0f);
            float dst_w = object_json.value("width", 0.0f);
            float dst_h = object_json.value("height", 0.0f);
            float rotation = object_json.value("rotation", 0.0f);

            // ⚡️ 关键修正：如果 Tiled 里没有拉伸尺寸，使用 Sprite 原始尺寸
            if (dst_w == 0 || dst_h == 0)
            {
                dst_w = sprite.getSize().x;
                dst_h = sprite.getSize().y;
            }

            // 3. 坐标转换 (Tiled 底部对齐 -> 引擎顶部对齐)
            glm::vec2 position = glm::vec2(x, y - dst_h);
            sprite.setSize(glm::vec2(dst_w, dst_h));

            // 4. 创建 GameObject
            auto game_object = std::make_unique<engine::object::GameObject>(
                *engine::core::Context::Current,
                object_json.value("name", "Object"));

            game_object->addComponent<engine::component::TransformComponent>(position, glm::vec2(1.0f), rotation);
            game_object->addComponent<engine::component::SpriteComponent>(std::move(sprite));

            scene->addGameObject(std::move(game_object));

            spdlog::info("成功加载对象: {}, 最终坐标: ({}, {}), 尺寸: {}x{}",
                         object_json.value("name", "unnamed"), position.x, position.y, dst_w, dst_h);
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

        // 必须确保这两个字段存在，因为 getTileDataByGid 强依赖它们
        tileset_data["firstgid"] = first_gid;
        tileset_data["file_path"] = std::string(tileset_path);

        _tileset_data[first_gid] = std::move(tileset_data);
    }
    engine::component::TileData LevelLoader::getTileDataByGid(int gid) const
    {
        if (gid == 0)
            return engine::component::TileData();

        auto tileset_it = _tileset_data.upper_bound(gid);
        if (tileset_it == _tileset_data.begin())
            return engine::component::TileData();
        --tileset_it;

        const auto &tileset = tileset_it->second;
        int first_gid = tileset["firstgid"].get<int>();
        int local_id = gid - first_gid;
        const std::string file_path = tileset.value("file_path", "");
        auto sprite = getSpriteByGid(gid);

        if (tileset.contains("type") && tileset["type"] == "tileset")// 单图片逻辑
        {
            const std::string texture_id = resolvePath(tileset["image"].get<std::string>(), file_path);
            auto size = sprite.getSize();
            glm::vec2 tex_size = glm::vec2(size.x, size.y);
            if (sprite.getSize().x > 0.0f && sprite.getSize().y > 0.0f)
            {
                tex_size = sprite.getSize();
            }
            else
            {
                tex_size = engine::core::Context::Current->getResourceManager().getTextureSize(texture_id);
                sprite.setSize(tex_size);
            }

            if (tex_size.x <= 0 || tex_size.y <= 0)
            {
                spdlog::error("无法获取纹理尺寸，ID: {}", texture_id);
                return engine::component::TileData();
            }
            // 关键：读取 Tileset 自己的瓦片尺寸
            int ts_tile_w = tileset.value("tilewidth", 0);
            int ts_tile_h = tileset.value("tileheight", 0);
            int columns = tileset.value("columns", 0);

            if (columns <= 0)
                return engine::component::TileData();

            // 2. 计算像素坐标
            float pixel_x = static_cast<float>((local_id % columns) * ts_tile_w);
            float pixel_y = static_cast<float>((local_id / columns) * ts_tile_h);

            // 3. 计算归一化 UV 矩形 (x, y, w, h) -> 范围 0.0 到 1.0
            // 这直接对应你 SpritePushConstants 里的 uv_rect
            glm::vec4 uv_rect{
                pixel_x / tex_size.x,
                pixel_y / tex_size.y,
                static_cast<float>(ts_tile_w) / tex_size.x,
                static_cast<float>(ts_tile_h) / tex_size.y};

            // 4. 返回精简后的 TileData
            return engine::component::TileData(uv_rect, engine::component::TileType::Normal, texture_id);
            // --- 核心修改结束 ---
        }
        else if (tileset.contains("type") && tileset["type"] == "tileset")
        {
            if (!tileset.contains("tiles"))
            {
                spdlog::error("Tileset 文件 {} 缺少 tiles 属性", tileset_it->first);
                return engine::component::TileData();
            }
            // 遍历 _tiles 数组， 根据id查找对应的瓦片
            const auto &tiles_json = tileset["tiles"];
            float w = tileset.value("tilewidth", 0.0f);
            float h = tileset.value("tileheight", 0.0f);
            for (const auto &tile_json : tiles_json)
            {
                auto tile_id = tile_json.value("id", 0);
                if (tile_id == local_id)
                {
                    if (!tile_json.contains("image"))
                    {
                        spdlog::error("Tileset 文件 {} 中 瓦片 {} 缺少 image 属性", tileset_it->first, tile_id);
                        return engine::component::TileData();
                    }
                    // 获取图片路径
                    const std::string texture_id = resolvePath(tile_json["image"].get<std::string>(), file_path);
                    // 确认图片尺寸
                    // 修正：从 tile_json 中安全获取宽高，默认偏移为 0

                    // 从 json 中获取源矩形
                    engine::utils::FRect source_rect{
                        glm::vec2{0.0f, 0.0f}, // 集合图块通常引用整张图
                        glm::vec2{w, h}};
                    engine::render::Sprite sprite{texture_id, source_rect};
                    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
                    return engine::component::TileData(uv_rect, engine::component::TileType::Normal, texture_id);
                }
            }
        }
        spdlog::error("图块集 {} 无法找到 GID: {} 对应的 瓦片", tileset_it->first, gid);
        return engine::component::TileData();
    }
    engine::render::Sprite LevelLoader::getSpriteByGid(int gid) const
    {
        if (gid == 0)
            return engine::render::Sprite();

        auto tileset_it = _tileset_data.upper_bound(gid);
        if (tileset_it == _tileset_data.begin())
            return engine::render::Sprite();
        --tileset_it;

        const auto &tileset = tileset_it->second;
        int first_gid = tileset["firstgid"].get<int>();
        int local_id = gid - first_gid;
        const std::string file_path = tileset.value("file_path", "");

        // 情况 A：单张大图（基于像素裁剪）
        if (tileset.contains("image"))
        {
            const std::string texture_id = resolvePath(tileset["image"].get<std::string>(), file_path);
            int ts_tile_w = tileset.value("tilewidth", 0);
            int ts_tile_h = tileset.value("tileheight", 0);
            int columns = tileset.value("columns", 0);

            if (columns <= 0)
                return engine::render::Sprite();

            engine::utils::FRect source_rect{
                glm::vec2{(local_id % columns) * ts_tile_w, (local_id / columns) * ts_tile_h},
                glm::vec2{(float)ts_tile_w, (float)ts_tile_h}};

            engine::render::Sprite sprite(texture_id, source_rect);
            sprite.setSize(glm::vec2(ts_tile_w, ts_tile_h)); // 必须设置初始逻辑尺寸
            return sprite;
        }
        // 情况 B：图像集合（Collection of Images，每个 Tile 一个文件）
        else if (tileset.contains("tiles"))
        {
            for (const auto &tile_json : tileset["tiles"])
            {
                if (tile_json.value("id", -1) == local_id)
                {
                    std::string img_path = tile_json.value("image", "");
                    std::string texture_id = resolvePath(img_path, file_path);
                    int ts_tile_w = tileset.value("tilewidth", 0);
                    int ts_tile_h = tileset.value("tileheight", 0);
                    // 这种情况下，source_rect 通常是全图
                    // 从 json 中获取源矩形
                    engine::utils::FRect source_rect{
                        glm::vec2{0.0f, 0.0f}, // 集合图块通常引用整张图
                        glm::vec2{ts_tile_w, ts_tile_h}};
                    engine::render::Sprite sprite(texture_id, source_rect);
                    return sprite;
                }
            }
        }

        return engine::render::Sprite();
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