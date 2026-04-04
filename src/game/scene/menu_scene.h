#pragma once
#include "../../engine/scene/scene.h"
#include "../../engine/ecs/registry.h"
#include "universe_editor.h"
#include <SDL3/SDL.h>

namespace game::scene
{
    class MenuScene : public engine::scene::Scene
    {
    public:
        MenuScene(const std::string &name, engine::core::Context &context, engine::scene::SceneManager &sceneManager);

        void init() override;
        void update(float delta_time) override;
        void render() override;
        void handleInput() override;
        void clean() override;

    private:
        engine::ecs::Registry ecs_registry;
        SDL_GLContext m_glContext = nullptr;

        bool m_showSettings = false;
        bool m_showDevMenu = false;
        int m_selectedLangIndex = 0;
        // 图形设置状态（与 config.json 同步）
        int  m_maxFpsSlider = 60;
        bool m_vsyncEnabled = true;
        // 按键绑定监听状态
        std::string m_keyListeningAction;
        int         m_keyListeningSlot   = 0;
        int         m_keyListeningFrames = 0;

        UniverseEditor m_universeEditor;

        void startGame();
        void openCharacterEditor();
        void openMapEditor();
        void renderMainMenu();
        void renderSettings();
        void renderPerformanceOverlay() const;
    };
}
