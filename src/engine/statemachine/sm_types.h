/**
 * sm_types.h  —  数据驱动状态机：核心数据结构
 *
 * 这里只存数据，不含任何逻辑。
 * 通过 sm_loader.h 读写 JSON，通过 state_controller.h 运行。
 */
#pragma once

#include <string>
#include <vector>
#include <map>

namespace engine::statemachine {

// ─────────────────────────────────────────────────────────────────────────────
//  帧区间类型
// ─────────────────────────────────────────────────────────────────────────────
enum class WindowType {
    Locked      = 0,  // 不可打断——动作刚开始，必须播完
    ComboWindow = 1,  // 连招触发区——此区间内按攻击键记录预输入
    Cancelable  = 2,  // 强制中断区——可被技能/闪避打断
};

static const char* kWindowTypeNames[] = { "Locked(锁定)", "ComboWindow(连招)", "Cancelable(可取消)" };

// ─────────────────────────────────────────────────────────────────────────────
//  帧区间（指定动作中某些帧的行为特性）
// ─────────────────────────────────────────────────────────────────────────────
struct FrameWindow {
    int        startFrame = 0;
    int        endFrame   = 0;
    WindowType type       = WindowType::Locked;
};

// ─────────────────────────────────────────────────────────────────────────────
//  帧事件（在特定帧触发的标签，由外部监听器处理）
// ─────────────────────────────────────────────────────────────────────────────
struct FrameEventData {
    int         frame = 0;
    std::string event;  // e.g. "play_sound:sword_swing", "spawn_vfx:slash", "shake_screen"
};

// ─────────────────────────────────────────────────────────────────────────────
//  根位移（Root Motion）：每帧的位移增量，注入物理引擎
// ─────────────────────────────────────────────────────────────────────────────
struct RootMotionFrame {
    int   frame = 0;
    float dx    = 0.0f;
    float dy    = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
//  转换条件（从一个状态跳转到另一个状态的规则）
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
//  触发器说明
//
//  触发器分三类：
//  1. KEY_*   —— 按键事件，需要 pushInput("KEY_X", time) 推入缓冲池
//               （只需在按下那一帧调用一次）
//  2. 持续状态  —— 每帧通过 update(dt, activeInputs, time) 传入
//               只要条件成立就放入 activeInputs
//  3. 瞬间事件  —— LAND / ANIM_END，条件成立当帧推入 activeInputs 即可
// ─────────────────────────────────────────────────────────────────────────────
static const char* kTriggerPresets[] = {
    // ── 按键事件（pushInput） ─────────────────────────────────────────────
    "KEY_ATTACK",   // 攻击键按下
    "KEY_JUMP",     // 跳跃键按下
    "KEY_DASH",     // 冲刺键按下
    "KEY_MOVE_L",   // 左移（持续，每帧加入 activeInputs）
    "KEY_MOVE_R",   // 右移（持续，每帧加入 activeInputs）
    "KEY_BLOCK",    // 格挡键按下
    "KEY_SKILL_1",  // 技能1
    "KEY_SKILL_2",  // 技能2
    "KEY_SKILL_3",  // 技能3
    // ── 动画事件 ─────────────────────────────────────────────────────────
    "ANIM_END",     // 非循环动画播放完毕（自动触发，无需手动加入）
    // ── 移动状态（持续，每帧 activeInputs） ──────────────────────────────
    "NO_INPUT",     // 无方向输入（A/D/←/→ 均未按下）
    "IS_MOVING",    // 正在移动（有方向输入）
    "IS_DASHING",   // 正在冲刺中
    "IS_ATTACKING", // 当前已处于某攻击状态（可用于防止重复进入）
    // ── 物理/重力状态（持续，每帧 activeInputs） ─────────────────────────
    "GROUNDED",     // 角色落地（physics vy ≈ 0 且 isGrounded）
    "AIRBORNE",     // 角色在空中（!GROUNDED）
    "RISING",       // 上升阶段（velocity.y < 0 在 y-up 坐标系，即 vy<0）
    "FALLING",      // 下坠阶段（velocity.y > 0，即重力方向）
    // ── 瞬间事件（只在发生那帧加入 activeInputs） ────────────────────────
    "LAND",         // 刚刚落地（上一帧 AIRBORNE，本帧 GROUNDED）
    "ON_WALL",      // 贴墙（水平速度接近0但有移动输入）
    nullptr
};

struct Transition {
    std::string trigger;           // 触发器名称（见 kTriggerPresets 或自定义）
    std::string targetState;       // 目标状态名
    int         priority     = 0;  // 优先级，越高越先被检查
    bool        requireWindow = false;  // 是否要求当前帧在指定区间内
    WindowType  windowType    = WindowType::ComboWindow;  // 要求区间类型
};

// ─────────────────────────────────────────────────────────────────────────────
//  状态节点（数据库中的一条状态记录）
// ─────────────────────────────────────────────────────────────────────────────
struct StateNode {
    std::string                  animationId;   // 帧编辑器中的动作名称
    bool                         loop   = true;
    int                          totalFrames = 8;  // 辅助编辑用（实际由动画组件驱动）
    std::vector<Transition>      transitions;
    std::vector<FrameWindow>     windows;
    std::vector<FrameEventData>  frameEvents;
    std::vector<RootMotionFrame> rootMotion;
};

// ─────────────────────────────────────────────────────────────────────────────
//  完整状态机数据（对应一个 .sm.json 文件）
// ─────────────────────────────────────────────────────────────────────────────
struct StateMachineData {
    std::string                       characterId;
    std::string                       initialState = "IDLE";
    std::map<std::string, StateNode>  states;
};

// ─────────────────────────────────────────────────────────────────────────────
//  状态控制器每帧输出结果
// ─────────────────────────────────────────────────────────────────────────────
struct UpdateResult {
    std::string              currentState;
    bool                     stateChanged   = false;
    float                    rootMotionDx   = 0.0f;
    float                    rootMotionDy   = 0.0f;
    std::vector<std::string> firedEvents;
    int                      currentFrame   = 0;
    float                    stateTimeRatio = 0.0f;  // 0..1，当前帧在动画中的进度
};

} // namespace engine::statemachine
