/**
 * state_controller.h  —  数据驱动状态控制器
 *
 * 职责：实时决策。不关心如何画动画，只关心：
 *   - 当前按了什么键
 *   - 配置表里说现在能去哪
 *   - 注册的自定义条件是否满足
 *
 * 使用方式（基础）：
 *   1. init(data, "IDLE")
 *   2. 每帧：push input → update(dt, activeInputs) → 读取 UpdateResult
 *   3. 根据 result.currentState 播放动画
 *   4. 根据 result.rootMotionDx/Dy 施加冲量
 *   5. 根据 result.firedEvents 触发音效/特效
 *
 * 使用方式（代码接入）：
 *   A. 注册自定义条件（与 .sm.json 中 CUSTOM 触发器配合使用）：
 *      sm.registerCondition("IS_LOW_HP", [](const StateController&) {
 *          return player.hp < player.maxHp * 0.25f;
 *      });
 *
 *   B. 监听状态切换：
 *      sm.setOnStateChanged([](const std::string& from, const std::string& to) {
 *          if (to == "HURT") playHurtSound();
 *      });
 *
 *   C. 监听帧事件（.sm.json 中 frameEvents 配置）：
 *      sm.setOnFrameEvent([](const std::string& event, int frame) {
 *          if (event == "play_sound:sword_swing") playSound("sword");
 *          if (event == "spawn_hitbox")           createHitbox(pos, 32.0f);
 *          if (event == "spawn_vfx:slash")        emitSlashVFX(pos);
 *      });
 *
 *   D. 强制跳转（被击、剧情等外部干预）：
 *      sm.forceTransition("HURT");
 */
#pragma once

#include "sm_types.h"
#include "input_buffer.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

namespace engine::statemachine {

class StateController {
public:
    // ── 回调函数类型 ─────────────────────────────────────────────────────
    /**
     * 自定义条件函数。
     * 每帧在 update() 内自动调用，若返回 true 则将触发器名称加入本帧激活指令。
     * 函数签名：bool(const StateController&)
     */
    using ConditionFn = std::function<bool(const StateController&)>;

    /**
     * 状态切换回调。
     * 发生跳转时调用，prevState 可能为空（初始化跳转）。
     * 函数签名：void(const std::string& prevState, const std::string& nextState)
     */
    using StateChangedFn = std::function<void(const std::string&, const std::string&)>;

    /**
     * 帧事件回调。
     * 每当动画播放到 .sm.json 中配置了 frameEvents 的帧时触发。
     * event 字段内容自定义，建议格式 "类别:参数"，如 "play_sound:sword"。
     * 函数签名：void(const std::string& event, int frame)
     */
    using FrameEventFn = std::function<void(const std::string&, int)>;

    StateController() = default;

    /** 绑定状态机数据并跳到初始状态 */
    void init(const StateMachineData* data, const std::string& initialState = "");

    /** 重置到初始状态（保留已注册的条件和回调）*/
    void reset();

    /**
     *  主更新函数，每帧调用。
     *  @param dt           帧时间（秒）
     *  @param activeInputs 本帧激活的指令列表（已按下 or 持续）
     *  @param time         游戏总时间（用于 InputBuffer 时间戳）
     *
     *  注意：注册的自定义条件在函数内部自动求值，无需手动加入 activeInputs。
     */
    UpdateResult update(float dt, const std::vector<std::string>& activeInputs, float time);

    /** 将一次按键压入指令缓冲池（通常在 isActionPressed 时调用）*/
    void pushInput(const std::string& action, float time);

    // ── 代码接入：自定义条件 ─────────────────────────────────────────────
    /**
     * 注册一个命名条件函数。
     * 在 .sm.json 的 Transition.trigger 字段填写同名字符串即可让状态机感知此条件。
     * 条件每帧自动求值；返回 true 时等价于该帧收到了对应触发器。
     *
     * 示例：
     *   sm.registerCondition("IS_LOW_HP", [&](const StateController&) {
     *       return m_hp < m_maxHp * 0.25f;
     *   });
     *   // 然后在状态机编辑器中，为某条 Transition 填写 trigger = "IS_LOW_HP"
     */
    void registerCondition(const std::string& triggerName, ConditionFn fn);

    /** 移除指定名称的条件（不存在时无操作）*/
    void unregisterCondition(const std::string& triggerName);

    /** 清除所有已注册的自定义条件 */
    void clearConditions();

    // ── 代码接入：事件回调 ──────────────────────────────────────────────
    /**
     * 设置状态切换回调（替换旧回调）。
     * prevState 在首次 init 跳转时为空字符串。
     *
     * 示例：
     *   sm.setOnStateChanged([this](const std::string& from, const std::string& to) {
     *       spdlog::debug("[SM] {} -> {}", from, to);
     *       if (to == "HURT")   startHurtFlash();
     *       if (to == "DEAD")   triggerDeathSequence();
     *       if (from == "JUMP") m_landingDust = true;
     *   });
     */
    void setOnStateChanged(StateChangedFn fn);

    /**
     * 设置帧事件回调（替换旧回调）。
     * event 内容来自 .sm.json 中每个 StateNode 的 frameEvents[].event 字段。
     * 建议使用 "类别:参数" 格式，如 "play_sound:sword_swing"、"spawn_vfx:slash"。
     *
     * 示例：
     *   sm.setOnFrameEvent([this](const std::string& event, int frame) {
     *       if (event == "play_sound:sword_swing") playSound("sword_swing");
     *       if (event == "spawn_hitbox")           createMeleeHitbox(getPos(), 48.0f);
     *       if (event == "spawn_vfx:slash")        emitSlashVFX(getPos(), getFacing());
     *       if (event == "shake_screen")           startScreenShake(0.3f, 8.0f);
     *   });
     */
    void setOnFrameEvent(FrameEventFn fn);

    // ── 查询 ──────────────────────────────────────────────────────────────
    const std::string& getCurrentState() const { return m_currentStateName; }
    int                getCurrentFrame() const { return m_currentFrame; }
    float              getStateTime()    const { return m_stateTime; }
    bool               isValid()         const { return m_data != nullptr && m_currentState != nullptr; }

    /** 返回当前帧所在的区间类型（Locked/Combo/Cancelable），-1 = 不在任何区间 */
    int currentWindowType() const;

    /**
     * 直接强制跳转到指定状态（用于外部事件，如被击、剧情干预等）。
     * 会触发 OnStateChanged 回调。
     * @warning 绕过所有转换条件和优先级检查，请谨慎使用。
     */
    void forceTransition(const std::string& stateName);

    /** 设置每帧时长（秒）——用于将 dt 折算成帧索引 */
    void setFrameDuration(float secondsPerFrame) { m_frameDuration = secondsPerFrame; }

private:
    const StateMachineData* m_data             = nullptr;
    const StateNode*        m_currentState     = nullptr;
    std::string             m_currentStateName;
    float                   m_stateTime        = 0.0f;  // 在当前状态已经过的秒数
    int                     m_currentFrame     = 0;
    int                     m_lastEventFrame   = -1;    // 已触发事件的最后帧（防重复）
    float                   m_frameDuration    = 0.1f;  // 100ms / 帧（可被动画组件覆盖）
    InputBuffer             m_inputBuffer;

    // ── 代码接入数据 ──────────────────────────────────────────────────────
    std::unordered_map<std::string, ConditionFn> m_conditions;  // 自定义条件注册表
    StateChangedFn  m_onStateChanged;   // 状态切换回调
    FrameEventFn    m_onFrameEvent;     // 帧事件回调

    void doTransition(const std::string& stateName, UpdateResult& result);
    bool tryTransitions(const std::vector<std::string>& activeInputs,
                        bool inComboOrCancel, float time, UpdateResult& result);
    void collectFrameEvents(int prevFrame, int newFrame, UpdateResult& result);
    void collectRootMotion(int prevFrame, int newFrame, UpdateResult& result);
};

} // namespace engine::statemachine
