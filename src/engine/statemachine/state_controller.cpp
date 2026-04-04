#include "state_controller.h"
#include <algorithm>
#include <cmath>

namespace engine::statemachine {

// ─────────────────────────────────────────────────────────────────────────────
void StateController::init(const StateMachineData* data, const std::string& initialState)
{
    m_data = data;
    m_inputBuffer.clear();
    std::string startState = initialState.empty()
        ? (data ? data->initialState : "") : initialState;
    m_currentStateName.clear();
    m_currentState  = nullptr;
    m_stateTime     = 0.0f;
    m_currentFrame  = 0;
    m_lastEventFrame = -1;
    if (data && !startState.empty())
        forceTransition(startState);
}

void StateController::reset()
{
    if (m_data)
        init(m_data, m_data->initialState);
}

// ─────────────────────────────────────────────────────────────────────────────
UpdateResult StateController::update(float dt,
    const std::vector<std::string>& activeInputs, float time)
{
    UpdateResult result;
    if (!m_data || !m_currentState)
    {
        result.currentState = m_currentStateName;
        return result;
    }

    // 更新输入缓冲池（清除过期）
    m_inputBuffer.update(time);

    // ── 求值自定义条件，满足则注入触发器 ───────────────────────────────
    // 每帧对注册的条件函数求值，true → 等价于收到该触发器名称的指令
    std::vector<std::string> injectInputs;
    for (const auto& [name, fn] : m_conditions)
    {
        if (fn && fn(*this))
            injectInputs.push_back(name);
    }

    // 计算前后帧
    float prevStateTime = m_stateTime;
    m_stateTime += dt;
    int totalFrames = std::max(1, m_currentState->totalFrames);
    float animDuration = totalFrames * m_frameDuration;

    int prevFrame = m_currentFrame;
    int newFrame  = static_cast<int>(m_stateTime / m_frameDuration);

    // 检查动画是否结束
    bool animEnded = false;
    if (!m_currentState->loop && newFrame >= totalFrames)
    {
        newFrame  = totalFrames - 1;
        animEnded = true;
    }
    else if (m_currentState->loop && newFrame >= totalFrames)
    {
        newFrame    = newFrame % totalFrames;
        m_stateTime = std::fmod(m_stateTime, animDuration);
    }
    m_currentFrame = newFrame;

    // ── 当前帧区间 ──────────────────────────────────────────────────────
    int winType = currentWindowType();  // -1 / 0(Locked) / 1(Combo) / 2(Cancelable)
    bool inComboOrCancel = (winType == 1 || winType == 2);

    // ── 尝试转换 ────────────────────────────────────────────────────────
    // 合并当前激活输入 + 缓冲池中的输入 + 自定义条件注入
    std::vector<std::string> effectiveInputs;
    effectiveInputs.reserve(activeInputs.size() + injectInputs.size());
    effectiveInputs.insert(effectiveInputs.end(), activeInputs.begin(), activeInputs.end());
    for (const auto& name : injectInputs)
    {
        if (std::find(effectiveInputs.begin(), effectiveInputs.end(), name) == effectiveInputs.end())
            effectiveInputs.push_back(name);
    }
    // 把缓冲池中未消耗的指令也加入（仅在连招/可取消窗口生效）
    if (inComboOrCancel)
    {
        for (const auto& preset : { "KEY_ATTACK", "KEY_JUMP", "KEY_DASH",
                                     "KEY_SKILL_1","KEY_SKILL_2","KEY_SKILL_3" })
        {
            if (m_inputBuffer.has(preset, time))
            {
                // 只加一次，避免重复
                if (std::find(effectiveInputs.begin(), effectiveInputs.end(), preset)
                    == effectiveInputs.end())
                    effectiveInputs.push_back(preset);
            }
        }
    }

    // ANIM_END 触发器
    if (animEnded)
        effectiveInputs.push_back("ANIM_END");

    // 收集根位移（在转换前，当前帧到新帧之间）
    collectRootMotion(prevFrame, newFrame, result);

    // 收集帧事件
    collectFrameEvents(prevFrame, newFrame, result);

    // 检查转换
    tryTransitions(effectiveInputs, inComboOrCancel, time, result);

    result.currentState    = m_currentStateName;
    result.currentFrame    = m_currentFrame;
    result.stateTimeRatio  = (animDuration > 0.0f)
        ? std::min(1.0f, m_stateTime / animDuration) : 0.0f;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
void StateController::pushInput(const std::string& action, float time)
{
    m_inputBuffer.push(action, time);
}

// ─────────────────────────────────────────────────────────────────────────────
// 自定义条件注册
// ─────────────────────────────────────────────────────────────────────────────
void StateController::registerCondition(const std::string& triggerName, ConditionFn fn)
{
    m_conditions[triggerName] = std::move(fn);
}

void StateController::unregisterCondition(const std::string& triggerName)
{
    m_conditions.erase(triggerName);
}

void StateController::clearConditions()
{
    m_conditions.clear();
}

void StateController::setOnStateChanged(StateChangedFn fn)
{
    m_onStateChanged = std::move(fn);
}

void StateController::setOnFrameEvent(FrameEventFn fn)
{
    m_onFrameEvent = std::move(fn);
}

// ─────────────────────────────────────────────────────────────────────────────
int StateController::currentWindowType() const
{
    if (!m_currentState) return -1;
    for (const auto& w : m_currentState->windows)
    {
        if (m_currentFrame >= w.startFrame && m_currentFrame <= w.endFrame)
            return static_cast<int>(w.type);
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
void StateController::forceTransition(const std::string& stateName)
{
    UpdateResult dummy;
    doTransition(stateName, dummy);
}

// ─────────────────────────────────────────────────────────────────────────────
void StateController::doTransition(const std::string& stateName, UpdateResult& result)
{
    if (!m_data) return;
    auto it = m_data->states.find(stateName);
    if (it == m_data->states.end()) return;

    const std::string prevState = m_currentStateName;
    m_currentStateName = stateName;
    m_currentState     = &it->second;
    m_stateTime        = 0.0f;
    m_currentFrame     = 0;
    m_lastEventFrame   = -1;
    result.stateChanged  = true;
    result.currentState  = stateName;

    // 通知状态切换回调
    if (m_onStateChanged)
        m_onStateChanged(prevState, stateName);
}

// ─────────────────────────────────────────────────────────────────────────────
bool StateController::tryTransitions(const std::vector<std::string>& activeInputs,
    bool inComboOrCancel, float time, UpdateResult& result)
{
    if (!m_currentState) return false;

    // 按优先级降序排序后检查
    std::vector<const Transition*> sorted;
    sorted.reserve(m_currentState->transitions.size());
    for (const auto& t : m_currentState->transitions)
        sorted.push_back(&t);
    std::sort(sorted.begin(), sorted.end(),
        [](const Transition* a, const Transition* b){ return a->priority > b->priority; });

    for (const Transition* t : sorted)
    {
        // 区间要求检查
        if (t->requireWindow)
        {
            int wt = currentWindowType();
            if (wt != static_cast<int>(t->windowType)) continue;
        }

        // 触发器检查
        bool triggered = false;
        for (const auto& input : activeInputs)
        {
            if (input == t->trigger) { triggered = true; break; }
        }
        if (!triggered) continue;

        // 消耗缓冲池中的对应指令
        m_inputBuffer.consume(t->trigger, time);

        doTransition(t->targetState, result);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
void StateController::collectFrameEvents(int prevFrame, int newFrame, UpdateResult& result)
{
    if (!m_currentState) return;
    // 只触发 prevFrame < event.frame <= newFrame 范围内的事件（防重复）
    for (const auto& fe : m_currentState->frameEvents)
    {
        if (fe.frame > prevFrame && fe.frame <= newFrame && fe.frame > m_lastEventFrame)
        {
            result.firedEvents.push_back(fe.event);
            // 直接回调帧事件，不必外部循环 firedEvents
            if (m_onFrameEvent)
                m_onFrameEvent(fe.event, fe.frame);
        }
    }
    if (newFrame > m_lastEventFrame) m_lastEventFrame = newFrame;
}

// ─────────────────────────────────────────────────────────────────────────────
void StateController::collectRootMotion(int prevFrame, int newFrame, UpdateResult& result)
{
    if (!m_currentState) return;
    for (const auto& rm : m_currentState->rootMotion)
    {
        if (rm.frame > prevFrame && rm.frame <= newFrame)
        {
            result.rootMotionDx += rm.dx;
            result.rootMotionDy += rm.dy;
        }
    }
}

} // namespace engine::statemachine
