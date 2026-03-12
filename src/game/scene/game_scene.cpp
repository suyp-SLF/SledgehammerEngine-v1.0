#include "game_scene.h"
#include "../../engine/object/game_object.h"
#include "../../engine/component/transform_component.h"
#include "../../engine/component/sprite_component.h"
#include "../../engine/core/context.h"
#include "../../engine/render/sprite_render_system.h"
#include "../../engine/render/parallax_render_system.h"
#include "../../engine/render/tilelayer_render_system.h"
#include "../../engine/resource/resource_manager.h"
#include "../../engine/input/input_manager.h"
#include "../../engine/render/camera.h"
#include "../../engine/world/world_config.h"
#include "../../engine/world/perlin_noise_generator.h"
#include "../../engine/world/chunk_manager.h"
#include <spdlog/spdlog.h>

namespace game::scene
{
    /**
     * @brief 构造函数，初始化游戏场景
     *
     * @param name 场景名称
     * @param context 引擎上下文对象
     * @param sceneManager 场景管理器对象
     *
     * @note 构造完成后会输出调试日志，包含场景名称
     * @note 日志中可能包含特殊字符如 \t, \r, \n
     */
    GameScene::GameScene(const std::string &name,
                         engine::core::Context &context,
                         engine::scene::SceneManager &sceneManager)
        : Scene(name, context, sceneManager)
    {
        spdlog::debug("GameScene '{}' 构造完成", name);
    }

    /**
     * @brief 初始化游戏场景
     *
     * 该方法用于初始化游戏场景的特定内容，包括创建测试对象和调用基类初始化。
     * 初始化过程中会捕获并记录可能出现的异常，然后重新抛出异常供上层处理。
     *
     * @throws std::exception 如果初始化过程中发生错误，会抛出标准异常
     *
     * @note 该方法会执行以下操作：
     *       1. 创建测试对象 (createTestObject)
     *       2. 调用基类 Scene 的初始化方法
     *       3. 记录初始化完成的调试信息
     *       4. 处理并记录初始化过程中可能出现的异常
     *
     * @warning 如果初始化失败，异常会被重新抛出，调用者需要处理可能的异常情况
     */
    void GameScene::init()
    {
        // 创建 ChunkManager（纹理图集ID需提前加载）
        chunk_manager = std::make_unique<engine::world::ChunkManager>("tileset_atlas", glm::ivec2(16, 16));
        // 加载世界配置
        engine::world::WorldConfig config;
        config.loadFromFile("assets/world_config.json");
        config.seed = 12345; // 或者从配置读取

        // 创建生成器
        auto generator = std::make_unique<engine::world::PerlinNoiseGenerator>(config);

        // 设置给 ChunkManager
        chunk_manager->setTerrainGenerator(std::move(generator));
    }
    void GameScene::update(float delta_time)
    {
        Scene::update(delta_time);

        // 根据玩家位置更新可见块
        glm::vec2 playerPos = {0.0, 0.0}; // 需要实现
        chunk_manager->updateVisibleChunks(playerPos, 8); // 视距8个块

        // 可以在这里处理瓦片变化（例如挖掘）
        // TO DO
    }
    /**
     * @brief 渲染游戏场景
     *
     * 该方法负责渲染当前游戏场景的所有内容。它首先调用基类的渲染逻辑，
     * 然后驱动渲染系统绘制所有已注册的 SpriteComponent。
     *
     * @note 此方法会处理所有可见的 SpriteComponent，包括处理制表符(\t)、回车符(\r)和换行符(\n)
     */
    void GameScene::render()
    {
        // 1. 调用基类逻辑（如果有必要）
        Scene::render();

        // 在渲染其他对象之前或之后渲染瓦片
        chunk_manager->renderAll(_context);

        // 2. 核心：驱动渲染系统绘制所有已注册的 SpriteComponent
        // 这里才是真正去调 Renderer -> SDL_Render/SDL_GPU 的地方
        _context.getParallaxRenderSystem().renderAll(_context);
        _context.getSpriteRenderSystem().renderAll(_context);
        _context.getTilelayerRenderSystem().renderAll(_context);
    }
    /**
     * @brief 处理游戏场景的输入事件
     * @details 该函数首先调用基类 Scene 的 handleInput() 方法处理基础输入逻辑，
     *          然后执行游戏场景特有的输入处理。支持的输入包括但不限于：
     *          - 键盘按键（\t, \r, \n等特殊字符）
     *          - 鼠标操作
     *          - 手柄输入
     * @note 该函数会覆盖基类的同名方法
     */
    void GameScene::handleInput()
    {
        Scene::handleInput();
        testCamera();
    }
    /**
     * @brief 清理游戏场景资源
     *
     * 该函数用于清理GameScene类中的所有资源，包括但不限于：
     * - 释放动态分配的内存
     * - 重置场景状态
     * - 清理图形和音频资源
     *
     * 注意：此函数会调用基类Scene的clean()方法以确保基类资源也被正确清理。
     *
     * @note 该函数不处理制表符(\t)、回车符(\r)或换行符(\n)等特殊字符
     */
    void GameScene::clean()
    {
        Scene::clean();
    }
    /**
     * @brief 创建测试对象
     *
     * 该方法用于在场景中创建测试对象网格。具体实现如下：
     * 1. 在1250x1250的区域内，以32像素为间隔创建对象
     * 2. 每个测试对象包含：
     *    - TransformComponent：用于设置对象位置
     *    - SpriteComponent：使用指定纹理渲染
     * 3. 使用日志记录创建过程
     *
     * @note 处理的字符包括\t、\r、\n等特殊字符
     */
    void GameScene::createTestObject()
    {
        spdlog::trace("GameScene 创建测试对象");
        for (int i = 0; i < 50; i += 32)
        {
            for (int j = 0; j < 50; j += 32)
            {
                auto test_object = std::make_unique<engine::object::GameObject>(_context, "test_object");
                // 添加组件
                test_object->addComponent<engine::component::TransformComponent>(glm::vec2(i, j));
                test_object->addComponent<engine::component::SpriteComponent>("assets/textures/Props/bubble1.svg", engine::utils::Alignment::CENTER);
                // 添加到场景中
                addGameObject(std::move(test_object));
            }
        }
        spdlog::trace("GameScene 测试对象创建完成");
    }

    void GameScene::testCamera()
    {
        auto &camera = engine::core::Context::Current->getCamera();
        auto &input_manager = engine::core::Context::Current->getInputManager();
        if (input_manager.isActionDown("move_up"))
            camera.move(glm::vec2(0, -1));
        if (input_manager.isActionDown("move_down"))
            camera.move(glm::vec2(0, 1));
        if (input_manager.isActionDown("move_left"))
            camera.move(glm::vec2(-1, 0));
        if (input_manager.isActionDown("move_right"))
            camera.move(glm::vec2(1, 0));
    }
}