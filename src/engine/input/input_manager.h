#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <SDL3/SDL_render.h>
#include <glm/vec2.hpp>
#include <variant>

namespace engine::core
{
    class Config;
}
namespace engine::render
{
    class Renderer;
}

namespace engine::input
{
    enum class ActionState
    {
        INACTIVE,
        PRESSED_THIS_FRAME,
        HELD_DOWN,
        RELEASED_THIS_FRAME
    };

    class InputManager
    {
    private:
        engine::render::Renderer *_renderer = nullptr;
        std::unordered_map<std::string, std::vector<std::string>> _actions_to_keyname_map;
        std::unordered_map<std::variant<SDL_Scancode, Uint32>, std::vector<std::string>> _input_to_action_map;

        std::unordered_map<std::string, ActionState> _action_states;

        bool _should_quit = false;
        glm::vec2 _mouse_position;
        float _mouse_wheel_delta = 0.0f;  // 本帧鼠标滚轮偏移，每帧重置

    public:
        InputManager(engine::render::Renderer *_renderer, const engine::core::Config *config);

        void update();

        // 动作状态检查
        bool isActionDown(const std::string &action_name) const;
        bool isActionPressed(const std::string &action_name) const;
        bool isActionReleased(const std::string &action_name) const;

        // 获取是否退出
        bool shouldQuit() const;
        void setShouldQuit(bool should_quit);

        // 获取鼠标位置
        glm::vec2 getMousePosition() const;
        glm::vec2 getLogicalMousePosition() const;

        // 获取本帧鼠标滚轮偏移（向上为正，向下为负）
        float getMouseWheelDelta() const { return _mouse_wheel_delta; }

    private:
        void processEvent(const SDL_Event &event);
        void initializeMappings(const engine::core::Config *config);
        void updateActionState(const std::string &action_name, bool is_input_active, bool is_repeat_event);
        SDL_Scancode scancodeFromString(const std::string &key_name) const;
        Uint8 MouseButtonUint8FromString(const std::string &button_name) const;
    };
}; // namespace engin::input