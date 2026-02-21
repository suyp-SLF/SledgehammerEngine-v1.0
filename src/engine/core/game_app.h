#pragma once

#include <memory>

struct SDL_Window;
struct SDL_Renderer;

namespace engine::resource
{
    class ResourceManager; // 资源管理器类
}

namespace engine::render
{
    class Renderer; // 渲染器类
    class Camera;   // 相机类
}

namespace engine::input
{
    class InputManager; // 输入管理器类
}

namespace engine::scene
{
    class SceneManager; // 场景管理器类
}

namespace engine::core
{
    class Time;   // 时间管理类
    class Config; // 游戏配置类
    class Context;
    /**
     * @brief GameApp 类，表示游戏应用程序的主类
     * 该类使用SDL库创建窗口和渲染器，并管理游戏的主循环状态
     */
    class GameApp final
    {
    private:
        SDL_Window *_window = nullptr; // SDL窗口指针，用于创建和管理游戏窗口
        bool _is_running = false;      // 游戏运行状态标志，true表示游戏正在运行

        // 引擎组件
        // 1. 最底层的配置和时间（无依赖）
        std::unique_ptr<engine::core::Config> _config;
        std::unique_ptr<engine::core::Time> _time;
        // 2. 资源管理（依赖设备，但生存期应长于依赖它的渲染组件）
        std::unique_ptr<engine::resource::ResourceManager> _resource_manager;
        // 3. 渲染器（持有 GPUDevice）
        std::unique_ptr<engine::render::Renderer> _renderer;
        // 4. 其它功能组件
        std::unique_ptr<engine::render::Camera> _camera;
        std::unique_ptr<engine::input::InputManager> _input_manager;
        // 5. 依赖以上所有内容的上下文和场景管理（最先析构）
        std::unique_ptr<engine::core::Context> _context;
        std::unique_ptr<engine::scene::SceneManager> _scene_manager;

    public:
        GameApp();  // 构造函数，初始化游戏应用程序
        ~GameApp(); // 析构函数，清理游戏应用程序资源

        void run(); // 运行游戏主循环

        // 禁止拷贝和移动
        GameApp(const GameApp &) = delete;
        GameApp &operator=(const GameApp &) = delete;
        GameApp(GameApp &&) = delete;
        GameApp &operator=(GameApp &&) = delete;

    private:
        [[nodiscard]] bool init();
        void handleEvents();
        void update(float delta_time);
        void render();
        void close();
        // 初始化函数
        [[nodiscard]] bool initConfig();
        [[nodiscard]] bool initSDL();
        [[nodiscard]] bool initTime();
        [[nodiscard]] bool initResourceManager();
        [[nodiscard]] bool initRenderer();
        [[nodiscard]] bool initCamera();
        [[nodiscard]] bool initInputManager();
        [[nodiscard]] bool initContext();
        [[nodiscard]] bool initSceneManager();
    };
}; // namespace engine::core