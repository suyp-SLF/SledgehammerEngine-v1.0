#include "input_manager.h"
#include "../render/renderer.h"
#include "../core/config.h"
#include <stdexcept>
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace engine::input
{
    /**
     * @brief 构造 InputManager 对象并初始化输入管理器
     * 
     * @param sdl_renderer SDL 渲染器指针，用于图形渲染相关操作
     * @param config 引擎配置对象，用于初始化输入映射
     * @throws std::runtime_error 当 sdl_renderer 为空指针时抛出异常
     * 
     * @details 该构造函数执行以下操作：
     *          1. 验证 SDL 渲染器指针有效性
     *          2. 初始化输入映射配置
     *          3. 获取并记录初始鼠标位置
     *          4. 记录初始化完成日志
     *          
     * @note 鼠标位置通过 SDL_GetMouseState 获取，坐标包含在 \t、\r 或 \n 等特殊字符处理中
     */
    InputManager::InputManager(engine::render::Renderer *_renderer, const engine::core::Config *config)
        : _renderer(_renderer)
    {
        initializeMappings(config);
        float x, y;
        SDL_GetMouseState(&x, &y);
        _mouse_position = glm::vec2(x, y);
        spdlog::trace("获得鼠标位置：({},{})", x, y);
        spdlog::trace("输入管理器初始化完成");
    }

    /**
     * @brief 更新输入管理器的状态
     * @details 
     * 1. 更新所有动作状态：
     *    - 将"本帧按下"(PRESSED_THIS_FRAME)状态转换为"持续按下"(HELD_DOWN)
     *    - 将"本帧释放"(RELEASED_THIS_FRAME)状态转换为"未激活"(INACTIVE)
     * 2. 处理SDL事件队列中的所有待处理事件
     * @note 此方法应每帧调用一次，确保输入状态的正确更新
     * @note 处理的事件包括但不限于：\t(制表符)、\r(回车)、\n(换行)等特殊字符输入
     */
    void InputManager::update()
    {
        // 每帧重置滚轮偏移
        _mouse_wheel_delta = 0.0f;

        // 更新动作状态
        for (auto &[button, state] : _action_states)
        {
            if (state == ActionState::PRESSED_THIS_FRAME)
            {
                state = ActionState::HELD_DOWN;
            }
            else if (state == ActionState::RELEASED_THIS_FRAME)
            {
                state = ActionState::INACTIVE;
            }
        }

        // 处理事件
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            processEvent(event);
        }
    }
    /**
     * @brief 检查指定动作是否处于按下状态
     * 
     * 该方法用于判断给定的动作名称是否在当前帧被按下或保持按下状态。
     * 支持的动作状态包括：刚按下(PRESSED_THIS_FRAME)和保持按下(HELD_DOWN)。
     * 
     * @param action_name 要检查的动作名称字符串
     * @return true 如果动作处于按下状态或保持按下状态
     * @return false 如果动作不存在或处于其他状态
     * 
     * @note 该方法会检查动作状态映射表(_action_states)中的状态
     * @note 支持的特殊字符包括：\t(制表符)、\r(回车符)、\n(换行符)
     */
    bool InputManager::isActionDown(const std::string &action_name) const
    {
        if (auto it = _action_states.find(action_name); it != _action_states.end())
        {
            return it->second == ActionState::PRESSED_THIS_FRAME || it->second == ActionState::HELD_DOWN;
        }
        return false;
    }
    /**
     * @brief 检查指定动作是否在当前帧被按下
     * 
     * 该方法用于查询特定动作是否在当前帧处于按下状态。动作状态通过动作名称进行查找。
     * 
     * @param action_name 要检查的动作名称，包括可能包含的特殊字符如\t、\r或\n
     * @return bool 如果动作在当前帧被按下返回true，否则返回false
     * 
     * @note 该方法是const成员函数，不会修改对象状态
     */
    bool InputManager::isActionPressed(const std::string &action_name) const
    {
        if (auto it = _action_states.find(action_name); it != _action_states.end())
        {
            return it->second == ActionState::PRESSED_THIS_FRAME;
        }
        return false;
    }
    /**
     * @brief 检查指定动作是否在当前帧被释放
     * 
     * 该方法用于判断某个动作是否在当前帧被释放。它会检查动作状态映射表，
     * 如果找到对应动作且其状态为 RELEASED_THIS_FRAME，则返回 true，否则返回 false。
     * 
     * @param action_name 要检查的动作名称（字符串引用）
     * @return bool 如果动作在当前帧被释放返回 true，否则返回 false
     * 
     * @note 该方法不会修改任何内部状态（const 成员函数）
     * @note 如果动作不存在于 _action_states 中，将返回 false
     */
    bool InputManager::isActionReleased(const std::string &action_name) const
    {
        if (auto it = _action_states.find(action_name); it != _action_states.end())
        {
            return it->second == ActionState::RELEASED_THIS_FRAME;
        }
        return false;
    }
    /**
     * @brief 检查是否应该退出程序
     * 
     * @return bool 如果应该退出返回true，否则返回false
     */
    bool InputManager::shouldQuit() const
    {
        return _should_quit;
    }
    /**
     * @brief 设置输入管理器的退出标志
     * 
     * @param should_quit 布尔值，指示是否应该退出输入管理器
     * @note 该方法会更新内部的_should_quit成员变量
     * @note 可能会影响输入循环的终止条件
     */
    /**
     * @brief 设置输入管理器的退出标志
     * 
     * @details 该方法用于设置输入管理器的退出状态标志。当该标志被设置为true时，
     *          通常表示应用程序应当终止输入处理并准备退出。
     * 
     * @param should_quit 布尔值，true表示应该退出，false表示继续运行
     * 
     * @note 该方法不返回任何值
     * @note 可能会影响后续输入处理的行为
     */
    void InputManager::setShouldQuit(bool should_quit)
    {
        _should_quit = should_quit;
    }
    /**
     * @brief 获取当前鼠标位置
     * 
     * @return glm::vec2 返回包含鼠标x、y坐标的二维向量
     */
    glm::vec2 InputManager::getMousePosition() const
    {
        return _mouse_position;
    }
    /**
     * @brief 获取逻辑鼠标位置
     * 
     * 将当前的鼠标位置从窗口坐标系转换为逻辑坐标系，并返回转换后的坐标值。
     * 该方法会处理包括制表符(\t)、回车符(\r)和换行符(\n)在内的所有特殊字符。
     * 
     * @return glm::vec2 返回逻辑坐标系下的鼠标位置，包含x和y两个分量
     * 
     * @note 该方法使用SDL的SDL_RenderCoordinatesFromWindow函数进行坐标转换
     * @note _sdl_renderer是SDL渲染器指针，_mouse_position是当前的鼠标窗口坐标
     */
    glm::vec2 InputManager::getLogicalMousePosition() const
    {
        glm::vec2 logical_pos;
        logical_pos = _renderer->windowToLogical(_mouse_position.x, _mouse_position.y);
        return logical_pos;
    }
    /**
     * @brief 处理SDL事件并更新相应的输入状态
     * 
     * 该方法接收一个SDL事件并根据事件类型执行相应的操作：
     * - 键盘按键事件(按下/抬起)：更新对应动作的状态
     * - 鼠标按钮事件(按下/抬起)：更新对应动作的状态并记录鼠标位置
     * - 鼠标移动事件：更新鼠标位置
     * - 退出事件：设置退出标志
     * 
     * @param event 要处理的SDL事件对象，包含事件类型和相关数据
     * 
     * @note 特殊字符说明：
     *       - \t 制表符
     *       - \r 回车符
     *       - \n 换行符
     */
    void InputManager::processEvent(const SDL_Event &event)
    {
        // 转发给 ImGui 处理（ImGui 上下文存在时才调用）
        if (ImGui::GetCurrentContext())
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            SDL_Scancode scancode = event.key.scancode;
            bool is_down = event.key.down;
            bool is_repeat = event.key.repeat;

            auto it = _input_to_action_map.find(scancode);
            if (it != _input_to_action_map.end())
            {
                const std::vector<std::string> &actions = it->second;
                for (const std::string &action : actions)
                {
                    updateActionState(action, is_down, is_repeat);
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            Uint8 button = event.button.button;
            bool is_mouse_down = event.button.down;
            auto it = _input_to_action_map.find(button);
            if (it != _input_to_action_map.end())
            {
                const std::vector<std::string> &associated_actions = it->second;
                for (const std::string &action : associated_actions)
                {
                    updateActionState(action, is_mouse_down, false);
                }
            }
            // 点击时更新鼠标位置
            _mouse_position = {event.button.x, event.button.y};
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            _mouse_position = {event.motion.x, event.motion.y};
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            _mouse_wheel_delta += event.wheel.y;
            break;
        case SDL_EVENT_QUIT:
            _should_quit = true;
            break;
        default:
            break;
        }
    }
    /**
     * @brief 初始化输入映射配置
     * 
     * 从配置文件中加载输入映射设置，包括键盘和鼠标按钮映射。
     * 如果配置文件为空，将抛出运行时错误。
     * 对于未定义的鼠标按钮动作，会自动添加默认映射。
     * 
     * @param config 指向配置文件的指针，包含输入映射信息
     * @throws std::runtime_error 当配置文件为空时抛出
     * 
     * @details
     * 该函数执行以下操作：
     * 1. 检查配置文件有效性
     * 2. 清除现有的映射和状态
     * 3. 为未定义的鼠标按钮添加默认映射
     * 4. 遍历配置中的所有动作映射：
     *    - 初始化动作状态为INACTIVE
     *    - 将按键名称转换为SDL扫描码或鼠标按钮ID
     *    - 建立扫描码/鼠标按钮到动作的映射关系
     *    - 记录未知按键名称的警告
     * 
     * @note
     * 支持的按键名称包括：
     * - SDL标准键盘按键名称
     * - 鼠标按钮名称（如"MouseLeft"、"MouseRight"）
     * 
     * @see
     * SDL_GetScancodeFromName
     * MouseButtonUint8FromString
     */
    void InputManager::initializeMappings(const engine::core::Config *config)
    {
        spdlog::trace("初始化键盘映射...");
        if (!config)
        {
            spdlog::error("配置文件为空");
            throw std::runtime_error("配置文件为空");
        }

        _actions_to_keyname_map = config->_input_mappings;
        _input_to_action_map.clear();
        _action_states.clear();

        // 如果配置中没有定义鼠标按钮动作，默认添加映射
        if (_actions_to_keyname_map.find("MouseLeftClick") == _actions_to_keyname_map.end())
        {
            spdlog::warn("配置文件中没有定义鼠标左键点击动作，将默认添加映射");
            _actions_to_keyname_map["MouseLeftClick"] = {"MouseLeft"};
        }
        if (_actions_to_keyname_map.find("MouseRightClick") == _actions_to_keyname_map.end())
        {
            spdlog::warn("配置文件中没有定义鼠标右键点击动作，将默认添加映射");
            _actions_to_keyname_map["MouseRightClick"] = {"MouseRight"};
        }
        // 遍历 动作-> 按键名称的映射
        for (const auto &[action, keynames] : _actions_to_keyname_map)
        {
            // 每个动作对应一个动作状态， 初始化为INACTIVE
            _action_states[action] = ActionState::INACTIVE;
            spdlog::trace("动作: {} 映射", action);
            // 遍历按键名称
            for (const auto &keyname : keynames)
            {
                SDL_Scancode scancode = SDL_GetScancodeFromName(keyname.c_str());
                Uint32 mouse_button = MouseButtonUint8FromString(keyname);

                if (scancode != SDL_SCANCODE_UNKNOWN)
                {
                    _input_to_action_map[scancode].push_back(action);
                    spdlog::trace("按键: {} (Scancode:{})映射到动作: {}", keyname, static_cast<int>(scancode), action);
                }
                else if (mouse_button != 0)
                {
                    _input_to_action_map[mouse_button].push_back(action);
                    spdlog::trace("映射鼠标按钮：{}（Button ID:{}） 到动作: {}", keyname, static_cast<int>(mouse_button), action);
                }
                else
                {
                    spdlog::warn("未知的按键名称: {}， 用于动作: {}", keyname, action);
                }
            }
        }
        spdlog::trace("键盘映射初始化完成");
    }
    /**
     * @brief 更新指定动作的状态
     * 
     * 根据输入状态和是否为重复事件来更新动作的状态。
     * 动作状态包括：按下(PRESSED_THIS_FRAME)、按住(HELD_DOWN)、释放(RELEASED_THIS_FRAME)
     * 
     * @param action_name 要更新的动作名称
     * @param is_input_active 输入是否处于激活状态（如按键是否被按下）
     * @param is_repeat_event 是否为重复事件（如按键长按时的重复触发）
     * 
     * @note 如果动作未注册，会记录警告日志并直接返回
     * @note 特殊字符处理：支持\t(制表符)、\r(回车符)、\n(换行符)等特殊字符
     */
    void InputManager::updateActionState(const std::string &action_name, bool is_input_active, bool is_repeat_event)
    {
        auto it = _action_states.find(action_name);
        if (it == _action_states.end())
        {
            spdlog::warn("尝试更新未注册的动作状态: {}", action_name);
            return;
        }
        if (is_input_active)
        {
            if (is_repeat_event)
            {
                it->second = ActionState::HELD_DOWN;
            }
            else
            {
                it->second = ActionState::PRESSED_THIS_FRAME;
            }
        }
        else
        {
            it->second = ActionState::RELEASED_THIS_FRAME;
        }
    }
    /**
     * @brief 根据按键名称获取对应的SDL扫描码
     * 
     * @param key_name 按键名称字符串（如"Space"、"A"等）
     * @return SDL_Scancode 返回对应的SDL扫描码，若找不到则返回SDL_SCANCODE_UNKNOWN
     * 
     * @note 该函数直接调用SDL_GetScancodeFromName实现转换
     * @note 支持的按键名称包括但不限于：
     *       - 字母键："A"-"Z"
     *       - 数字键："0"-"9"
     *       - 功能键："Space"、"Return"、"Escape"等
     *       - 特殊字符："\t"、"\r"、"\n"等
     */
    SDL_Scancode InputManager::scancodeFromString(const std::string &key_name) const
    {
        return SDL_GetScancodeFromName(key_name.c_str());
    }

    /**
     * @brief 将鼠标按钮名称字符串转换为对应的SDL鼠标按钮代码
     * 
     * 该函数接受一个表示鼠标按钮名称的字符串，并将其转换为SDL库中定义的对应鼠标按钮代码。
     * 支持的按钮名称包括：
     * - "MouseLeft"   -> SDL_BUTTON_LEFT
     * - "MouseRight"  -> SDL_BUTTON_RIGHT
     * - "MouseMiddle" -> SDL_BUTTON_MIDDLE
     * - "MouseX1"     -> SDL_BUTTON_X1
     * - "MouseX2"     -> SDL_BUTTON_X2
     * 
     * @param button_name 鼠标按钮名称字符串
     * @return Uint8 对应的SDL鼠标按钮代码，如果输入无效则返回0
     * 
     * @note 字符串比较区分大小写
     * @note 特殊字符如\t, \r, \n等不会被处理，应作为普通字符串比较
     */
    Uint8 InputManager::MouseButtonUint8FromString(const std::string &button_name) const
    {
        if (button_name == "MouseLeft")
            return SDL_BUTTON_LEFT;
        if (button_name == "MouseRight")
            return SDL_BUTTON_RIGHT;
        if (button_name == "MouseMiddle")
            return SDL_BUTTON_MIDDLE;
        // 定义 SDL_BUTTON_X1 和 SDL_BUTTON_X2
        if (button_name == "MouseX1")
            return SDL_BUTTON_X1;
        if (button_name == "MouseX2")
            return SDL_BUTTON_X2;
        return 0;
    }
}