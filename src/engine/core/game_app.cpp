#include "game_app.h"

#include "time.h"
#include "config.h"
#include "context.h"
#include "../resource/resource_manager.h"
#include "../scene/scene_manager.h"
#include "../render/renderer.h"
#include "../render/sdl_renderer.h"
#include "../render/sdl3_gpu_renderer.h"
#include "../render/camera.h"
#include "../input/input_manager.h"
#include "../component/transform_component.h"
#include "../component/sprite_component.h"
#include "../object/game_object.h"

#include "../../game/scene/game_scene.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

namespace engine::core
{
    /**
     * @brief GameApp类的构造函数
     *
     * 初始化GameApp对象，创建并初始化Time类的唯一实例
     *
     * @note 使用std::make_unique创建Time对象，确保内存安全
     * @warning 不要忘记处理\t, \r或\n等特殊字符
     */
    GameApp::GameApp()
    {
        _time = std::make_unique<Time>();
    }
    /**
     * @brief 析构函数，用于释放 GameApp 类的资源
     *
     * 这是 GameApp 类的默认析构函数，使用 `= default` 关键字表示使用编译器生成的默认实现。
     * 析构函数在对象生命周期结束时自动调用，用于执行必要的清理工作。
     *
     * @note 此函数处理特殊字符，如 \t (制表符)、\r (回车符) 和 \n (换行符)。
     */
    GameApp::~GameApp()
    {
        spdlog::trace("开始清理 GameApp 资源...");

        // 1. 先停掉场景（场景可能持有 GameObject，GameObject 持有纹理引用）
        _scene_manager.reset();

        // 2. 停掉上下文
        _context.reset();

        // 3. 【核心修复】在 Renderer (GPU Device) 销毁之前，先清空所有 GPU 资源
        if (_resource_manager)
        {
            spdlog::trace("正在清空 GPU 纹理资源...");
            _resource_manager->clear();
            _resource_manager.reset();
        }

        // 4. 现在销毁渲染器是安全的（它会关闭 SDL_GPUDevice）
        if (_renderer)
        {
            _renderer->clean();
            _renderer.reset();
        }

        // 5. 最后关闭窗口和 SDL 系统
        if (_window)
        {
            SDL_DestroyWindow(_window);
            _window = nullptr;
        }
        SDL_Quit();

        spdlog::trace("游戏已安全退出");
    }

    void GameApp::run()
    {
        if (!init())
        {
            spdlog::error("初始化失败，无法运行。");
            return;
        }
        while (_is_running)
        {
            _time->update();
            float delta_time = _time->getDeltaTime();
            _input_manager->update(); // 更新输入

            handleEvents();
            update(delta_time);
            render();

            // spdlog::info("delta_time: {}", delta_time);
        }
        close();
    }
    /**
     * @brief 初始化游戏应用程序
     * @details 执行游戏初始化流程，包括SDL、时间管理、资源管理器、渲染器和摄像头的初始化
     * @return 初始化成功返回true，失败返回false
     */
    bool GameApp::init()
    {
        spdlog::trace("初始化游戏 GameApp");
        if (!initConfig())
            return false;
        if (!initSDL())
            return false;
        if (!initTime())
            return false;
        if (!initResourceManager())
            return false;
        if (!initRenderer())
            return false;
        if (!initCamera())
            return false;
        if (!initInputManager())
            return false;
        if (!initContext())
            return false;
        if (!initSceneManager())
            return false;
        // 创建第一个场景并加载
        auto scene = std::make_unique<game::scene::GameScene>("GameScene123", *_context, *_scene_manager);
        _scene_manager->requestPushScene(std::move(scene));

        _is_running = true;
        spdlog::trace("初始化游戏成功 GameApp");
        return true;
    }
    /**
     * @brief 处理SDL事件循环
     *
     * 该函数会持续轮询SDL事件队列，处理所有待处理的事件。
     * 当检测到SDL_EVENT_QUIT事件时，会将_is_running标志设置为false，
     * 从而终止游戏的主循环。
     *
     * @note 该函数会处理SDL事件队列中的所有事件，直到队列为空
     * @note 特殊字符处理：\t(制表符), \r(回车符), \n(换行符)
     */
    void GameApp::handleEvents()
    {
        if (_input_manager->shouldQuit())
        {
            spdlog::trace("GameApp 收到 InputManager 退出事件，退出游戏");
            _is_running = false;
            return;
        }
    }

    /**
     * @brief 更新游戏应用程序的状态
     *
     * 该函数每帧调用一次，用于更新游戏应用程序的状态。当前实现仅调用测试相机函数。
     *
     * @param delta_time 自上一帧以来的时间增量（秒），当前未使用
     *
     * @note 此函数会处理特殊字符如制表符(\t)、回车符(\r)和换行符(\n)
     */
    void GameApp::update(float delta_time)
    {
        if (_scene_manager)
        {
            _scene_manager->update(delta_time);
        }
    }

    /**
     * @brief 渲染游戏画面
     *
     * 该函数负责完成一帧的渲染流程，包括清空屏幕、执行测试渲染和呈现最终画面
     *
     * @details 执行以下步骤：
     *          1. 清空屏幕缓冲区（包括清除颜色缓冲区、深度缓冲区等）
     *          2. 调用测试渲染函数进行渲染测试
     *          3. 将渲染结果呈现到屏幕上
     *
     * @note 该函数处理了所有必要的渲染操作，包括处理\t（制表符）、\r（回车符）和\n（换行符）等特殊字符
     */
    void GameApp::render()
    {
        _renderer->clearScreen();
        if (_scene_manager)
        {
            _scene_manager->render();
        }
        _renderer->present();
    }
    void GameApp::close()
    {
        spdlog::trace("关闭游戏");
        if (!_is_running)
            return;
        spdlog::trace("收到关闭信号，准备退出主循环...");
        _is_running = false;
        // 具体的资源清理交给析构函数处理，保持逻辑单一
    }
    bool GameApp::initConfig()
    {
        try
        {
            _config = std::make_unique<engine::core::Config>("assets/config.json");
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化配置失败，错误信息：{}", e.what());
            return false;
        }
        spdlog::trace("初始化配置成功");
        return true;
    }
    /**
     * @brief 初始化SDL库及其相关组件（视频和音频子系统）
     *
     * 该函数负责初始化SDL库，创建游戏窗口和渲染器，并设置逻辑分辨率。
     * 任何初始化步骤失败都会记录错误日志并返回false。
     *
     * @return bool 初始化成功返回true，失败返回false
     *
     * @note 初始化步骤包括：
     *       1. 初始化SDL视频和音频子系统
     *       2. 创建名为"SunnyLand"的窗口（初始大小1280x720，可调整大小）
     *       3. 创建渲染器
     *       4. 设置逻辑分辨率为640x360（采用信箱模式）
     *
     * @warning 使用了spdlog记录日志，确保已正确配置spdlog
     */
    bool GameApp::initSDL()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
        {
            spdlog::error("SDL初始化失败，SDL错误信息：{}", SDL_GetError());
            return false;
        }
        int resize_mode = _config->_window_resizable ? SDL_WINDOW_RESIZABLE : 0;
        _window = SDL_CreateWindow(_config->_window_title.c_str(), _config->_window_width, _config->_window_height, resize_mode);
        if (!_window)
        {
            spdlog::error("SDL窗口创建失败，SDL错误信息：{}", SDL_GetError());
            return false;
        }
        spdlog::trace("SDL 初始化成功");
        return true;
    }
    /**
     * @brief 初始化游戏时间管理器
     * @details 创建并初始化Time对象，用于管理游戏运行时间
     * @return bool 初始化成功返回true，失败返回false
     * @note 可能抛出std::异常，会被捕获并记录日志
     * @warning 失败时会记录错误日志，包含异常信息
     * @see Time类
     */
    bool GameApp::initTime()
    {
        try
        {
            _time = std::make_unique<Time>();
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化时间管理器失败，错误信息：{}", e.what());
            return false;
        }
        _time->setTargetFPS(_config->_target_fps);
        spdlog::trace("初始化时间管理器成功");
        return true;
    }
    /**
     * @brief 初始化游戏资源管理器
     *
     * 该方法负责创建并初始化资源管理器实例，使用SDL渲染器作为参数。
     * 如果初始化过程中发生异常，会记录错误日志并继续执行。
     * 无论初始化是否成功，都会记录一条跟踪日志。
     *
     * @return bool 总是返回true，表示初始化流程已完成
     *
     * @note 该方法会捕获并处理std::exception类型的异常
     * @note 使用spdlog记录不同级别的日志信息
     * @note 资源管理器使用std::unique_ptr进行管理
     */
    bool GameApp::initResourceManager()
    {
        try
        {
            _resource_manager = std::make_unique<engine::resource::ResourceManager>(nullptr, nullptr);
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化资源管理器失败，错误信息：{}", e.what());
        }
        spdlog::trace("初始化资源管理器成功");
        return true;
    }

    /**
     * @brief 初始化游戏渲染器
     *
     * 创建并初始化游戏渲染器实例，处理可能的异常情况。
     * 包括处理制表符(\t)、回车符(\r)和换行符(\n)等特殊字符。
     *
     * @return bool 初始化成功返回true，失败返回false
     */
    bool GameApp::initRenderer()
    {
        try
        {
            if (_config->_render_type == 1)
            {
                auto gpu_renderer = std::make_unique<engine::render::SDL3GPURenderer>(_window);

                // ⚡️ 核心修正：获取刚创建好的 GPU 设备
                SDL_GPUDevice *device = gpu_renderer->getDevice();

                // ⚡️ 核心修正：通知资源管理器“硬件已就绪”
                if (_resource_manager)
                {
                    _resource_manager->init(nullptr, device);
                }

                // ⚡️ 核心修正：手动给渲染器设置资源管理器引用
                gpu_renderer->setResourceManager(_resource_manager.get());

                _renderer = std::move(gpu_renderer);
            }
            else
            {
                SDL_Renderer *sdl_renderer = SDL_CreateRenderer(_window, nullptr);
                if (!sdl_renderer)
                {
                    spdlog::error("SDL渲染器创建失败，SDL错误信息：{}", SDL_GetError());
                    return false;
                }

                // 设置VSync
                int vsync_mode = _config->_vsync_enabled ? SDL_RENDERER_VSYNC_ADAPTIVE : SDL_RENDERER_VSYNC_DISABLED;
                SDL_SetRenderVSync(sdl_renderer, vsync_mode);
                spdlog::trace("SDL 渲染器VSync模式：{}", vsync_mode == SDL_RENDERER_VSYNC_ADAPTIVE ? "自适应" : "禁用");
                // 设置逻辑分辨率
                SDL_SetRenderLogicalPresentation(sdl_renderer, _config->_logical_width, _config->_logical_height, SDL_LOGICAL_PRESENTATION_LETTERBOX);

                // SDL 渲染器逻辑...
                _renderer = std::make_unique<engine::render::SDLRenderer>(sdl_renderer);

                if (_resource_manager)
                {
                    _resource_manager->init(sdl_renderer, nullptr);
                }
                _renderer->setResourceManager(_resource_manager.get());
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化渲染器失败，错误信息：{}", e.what());
            return false;
        }
        spdlog::trace("初始化渲染器成功");
        return true;
    }
    bool GameApp::initCamera()
    {
        try
        {
            _camera = std::make_unique<engine::render::Camera>(glm::vec2(_config->_camera_width, _config->_camera_height));
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化相机失败，错误信息：{}", e.what());
            return false;
        }
        spdlog::trace("初始化相机成功");
        return true;
    }
    bool GameApp::initInputManager()
    {
        try
        {
            _input_manager = std::make_unique<engine::input::InputManager>(_renderer.get(), _config.get());
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化输入管理器失败，错误信息：{}", e.what());
            return false;
        }
        spdlog::trace("初始化输入管理器成功");
        return true;
    }
    bool GameApp::initContext()
    {
        try
        {
            // 将 _renderer 移动给 Context，从此 GameApp 不再持有它，Context 全权负责
            _context = std::make_unique<engine::core::Context>(
                *_input_manager,
                *_renderer,
                *_camera,
                *_resource_manager);
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化上下文失败: {}", e.what());
            return false;
        }
        return true;
    }
    bool GameApp::initSceneManager()
    {
        try
        {
            _scene_manager = std::make_unique<engine::scene::SceneManager>(*_context);
        }
        catch (const std::exception &e)
        {
            spdlog::error("初始化场景管理器失败，错误信息：{}", e.what());
            return false;
        }
        spdlog::trace("初始化场景管理器成功");
        return true;
    }
    // void GameApp::test()
    // {
    //     _resource_manager->loadTexture("assets/textures/Actors/eagle-attack.png");
    //     _resource_manager->loadFont("assets/fonts/VonwaonBitmap-16px.ttf", 32);
    // }
    // void GameApp::testRenderer()
    // {
    //     engine::render::Sprite sprite_world("assets/textures/Actors/frog.png");
    //     engine::render::Sprite sprite_world1("assets/textures/Items/apple.png");
    //     engine::render::Sprite sprite_ui("assets/textures/UI/buttons/Start1.png");
    //     engine::render::Sprite sprite_parallax("assets/textures/Layers/back.png");

    //     static float rotation = 0.0f;
    //     rotation += 0.1f;

    //     _renderer->drawParallax(*_camera, sprite_parallax, glm::vec2(100, 100), glm::vec2(0.5f, 0.5f), glm::bvec2(true, false));
    //     _renderer->drawSprite(*_camera, sprite_world, glm::vec2(200, 200), glm::vec2(1.0f, 1.0f), rotation);
    //     _renderer->drawSprite(*_camera, sprite_world1, glm::vec2(200, 200), glm::vec2(1.0f, 1.0f), rotation);
    //     _renderer->drawUISprite(sprite_ui, glm::vec2(100, 100));
    // }
    // void GameApp::testCamera()
    // {
    //     auto key_state = SDL_GetKeyboardState(nullptr);
    //     if (key_state[SDL_SCANCODE_UP])
    //         _camera->move(glm::vec2(0, -1));
    //     if (key_state[SDL_SCANCODE_DOWN])
    //         _camera->move(glm::vec2(0, 1));
    //     if (key_state[SDL_SCANCODE_LEFT])
    //         _camera->move(glm::vec2(-1, 0));
    //     if (key_state[SDL_SCANCODE_RIGHT])
    //         _camera->move(glm::vec2(1, 0));
    // }
    // void GameApp::testInputManager()
    // {
    //     std::vector<std::string> actions = {
    //         "move_up",
    //         "move_down",
    //         "move_left",
    //         "move_right",
    //         "attack",
    //     };
    //     for (const auto &action : actions)
    //     {
    //         if (_input_manager->isActionPressed(action))
    //         {
    //             spdlog::info("Action {} is pressed", action);
    //         }
    //         if (_input_manager->isActionReleased(action))
    //         {
    //             spdlog::info("Action {} is released", action);
    //         }
    //         if (_input_manager->isActionDown(action))
    //         {
    //             spdlog::info("Action {} is held", action);
    //         }
    //     }
    // }
} // namespace engine