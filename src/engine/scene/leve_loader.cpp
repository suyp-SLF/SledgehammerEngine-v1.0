#include "leve_loader.h"
#include "../object/game_object.h"
#include "../component/transform_component.h"
#include "../component/parallax_component.h"
#include "../scene/scene.h"
#include "../core/context.h"
#include <fstream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <glm/vec2.hpp>
namespace engine::scene
{

    /** 加载关卡文件 */
    bool LevelLoader::loadLevel(const std::string &level_path, Scene *scene)
    {
        _map_path = level_path;
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
        // 加载数据层文件 Layers
        // 判断是否存在以及是否是数组类型
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
        auto texture_id = resolvePath(image_path);
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
        //
    }
    void LevelLoader::loadObjectLayer(const nlohmann::json &layer_json, Scene *scene)
    {
    }
    std::string LevelLoader::resolvePath(std::string_view image_path)
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
            fs::path map_dir = fs::path(_map_path).parent_path();

            /* * 4. 使用 lexically_normal() 代替 canonical()
             * - lexically_normal: 仅逻辑处理 "../" 和 "./"，不要求文件必须存在。
             * - canonical: 要求文件必须存在，否则抛出异常（过于严格）。
             */
            return (map_dir / img_p).lexically_normal().string();
        }
        catch (const fs::filesystem_error &e)
        {
            // 记录更详细的错误信息
            spdlog::error("路径拼接异常 | 输入: {} | 错误: {}", image_path, e.what());
            return std::string(image_path);
        }
    }
}