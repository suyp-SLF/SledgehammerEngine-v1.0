#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp> // JSON library forward declaration

namespace engine::core
{
    /**
     * @class Config
     * @brief 配置管理类，用于管理应用程序的各项设置参数
     * 
     * 该类负责管理应用程序的窗口设置、图形设置、性能设置、音频设置以及输入映射等配置项。
     * 支持从JSON文件加载配置和将配置保存到JSON文件。
     * 
     * 核心功能：
     * - 管理窗口相关设置（标题、尺寸、可调整性）
     * - 管理图形设置（垂直同步）
     * - 管理性能设置（目标帧率）
     * - 管理音频设置（音乐和音效音量）
     * - 管理输入映射（动作名称到SDL Scancode的映射）
     * - 支持从JSON文件加载和保存配置
     * 
     * 构造函数参数：
     * @param json_path JSON配置文件的路径，用于初始化配置项
     * 
     * 使用限制：
     * - 该类不可拷贝和移动（拷贝构造函数和移动构造函数已被删除）
     * - 配置文件的格式必须符合JSON规范
     * 
     * 示例用法：
     * @code
     * // 从JSON文件创建配置
     * Config config("settings.json");
     * 
     * // 修改配置项
     * config.window_width = 1920;
     * config.window_height = 1080;
     * 
     * // 保存配置到文件
     * config.saveToFile("settings.json");
     * @endcode
     */
    class Config final
    {
    public:
        // 窗口设置
        std::string _window_title = "SunnyLand";
        int _window_width = 1920; // 窗口大小
        int _window_height = 1080;
        int _logical_width = 1280; // 逻辑分辨路
        int _logical_height = 720;
        int _camera_width = 1280; // 摄像机视口大小
        int _camera_height = 720;
        bool _window_resizable = true;

        // 图形设置
        int _render_type = 0; // 渲染类型
        bool _vsync_enabled = true;
        // 性能设置
        int _target_fps = 60;
        // 音频设置
        float _music_volume = 0.5f;
        float _sfx_volume = 0.5f;

        // 存储动作名称到 SDL Scancode 名称列表的映射
        std::unordered_map<std::string, std::vector<std::string>> _input_mappings = {
            {"move_left", {"A", "Left"}},
            {"move_right", {"D", "Right"}},
            {"move_up", {"W", "Up"}},
            {"move_down", {"S", "Down"}},
            {"jump", {"J", "Space"}},
            {"attack", {"K", "MouseLeft"}},
            {"pause", {"P", "Escape"}},
            {"open_inventory", {"E"}},
            {"open_map", {"M"}}};
        explicit Config(const std::string &json_path);
        // 删除拷贝和移动
        Config(const Config &) = delete;
        Config &operator=(const Config &) = delete;
        Config(Config &&) = delete;
        Config &operator=(Config &&) = delete;

        bool loadFromFile(const std::string &json_path);
        [[nodiscard]] bool saveToFile(const std::string &json_path) const;

    private:
        void fromJson(const nlohmann::json  &json);
        nlohmann::ordered_json toJson() const;
    };
}; // namespace engine::core
