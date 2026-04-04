# 角色状态机接入接口文档

更新日期：2026-04-05

---

## 一、模块分布

```
engine/statemachine/
  sm_types.h              — POD 数据结构（StateMachineData / StateNode / Transition / FrameWindow / FrameEventData …）
  sm_loader.h / .cpp      — JSON 序列化/反序列化（*.sm.json 读写）
  input_buffer.h / .cpp   — 带时间戳的按键缓冲池（支持 0.2 s 预输入）
  state_controller.h/.cpp — 每帧决策：当前状态 → 下一状态，触发帧事件回调

game/statemachine/
  character_sm_setup.h        — setupCharacterSM() 接口声明（各角色共用派发入口）
  ghost_swordsman_sm.cpp      — 鬼剑士专属逻辑（setOnStateChanged + setOnFrameEvent）
  ghost_swordsman_example.cpp — 参考文档（不参与编译）
```

调用链：
```
game_scene.cpp
  └── loadPlayerSM()        读取 .sm.json → sm.init() → setupCharacterSM()
  └── tickPlayerSM()        每帧：activeInputs → sm.update() → 应用 result
```

---

## 二、核心接口速查

### 2.1 setupCharacterSM — 角色派发入口

```cpp
// game/statemachine/character_sm_setup.h
namespace game::statemachine {

bool setupCharacterSM(
    const std::string&                     characterId,  // character.json 里的 "id" 字段
    engine::statemachine::StateController& sm,           // 已 init() 完成的状态机控制器
    engine::object::GameObject*            actor,        // 被控角色（用于取 Component）
    engine::core::Context&                 context);     // 引擎 Context（取 InputManager / Time 等）

} // namespace game::statemachine
```

- 返回 `true`：找到对应角色的专属设置，回调已注册。
- 返回 `false`：未知 characterId，调用方回退到通用默认处理（ATTACK 系列状态自动锁定控制器）。

---

### 2.2 setOnStateChanged — 状态切换回调模板

```cpp
sm.setOnStateChanged([actor](const std::string& from, const std::string& to)
{
    using CC = engine::component::ControllerComponent;

    const bool enterAttack = (to.rfind("ATTACK", 0) == 0);
    const bool leaveAttack = (from.rfind("ATTACK", 0) == 0);

    if (auto* ctrl = actor ? actor->getComponent<CC>() : nullptr)
    {
        if (enterAttack && !leaveAttack)
            ctrl->setEnabled(false);   // 进入攻击：锁定 WASD，root_motion 驱动位移
        else if (leaveAttack && !enterAttack)
            ctrl->setEnabled(true);    // 离开攻击：恢复 WASD 移动
    }

    spdlog::info("[XXX SM] {} → {}", from.empty() ? "(init)" : from, to);
});
```

`from` 在首次 init 跳转时为空字符串，建议用 `from.empty() ? "(init)" : from` 输出。

---

### 2.3 setOnFrameEvent — 帧事件回调模板

```cpp
sm.setOnFrameEvent([actor](const std::string& event, int frame)
{
    using CC  = engine::component::ControllerComponent;
    using PC  = engine::component::PhysicsComponent;
    (void)frame;  // 如不需要帧号可忽略

    // ── spawn_hitbox:N ── 生成近战判定盒（N = 连招段数，1-4）
    if (event.rfind("spawn_hitbox:", 0) == 0)
    {
        const int   comboIdx = std::stoi(event.substr(13));  // "spawn_hitbox:" = 13 chars
        const float hitboxW  = 40.0f + static_cast<float>(comboIdx) * 4.0f;
        const float hitboxH  = 36.0f;

        float facingSign = 1.0f;
        if (auto* ctrl = actor ? actor->getComponent<CC>() : nullptr)
            facingSign = (ctrl->getFacingDirection() == CC::FacingDirection::Left) ? -1.0f : 1.0f;

        glm::vec2 actorPos{0.0f, 0.0f};
        if (auto* phys = actor ? actor->getComponent<PC>() : nullptr)
            actorPos = phys->getPosition();

        // TODO: 接入战斗系统后替换为：
        //   combatSys->createMeleeHitbox(
        //       actorPos + glm::vec2(hitboxW * 0.5f * facingSign, 0),
        //       hitboxW, hitboxH, comboIdx);
        spdlog::info("[XXX SM] spawn_hitbox:{} @ ({:.0f},{:.0f}) facing={:.0f} w={:.0f}",
                     comboIdx, actorPos.x, actorPos.y, facingSign, hitboxW);
    }

    // ── play_sound:xxx ── tickPlayerSM 统一播放，此处仅记诊断日志
    if (event.rfind("play_sound:", 0) == 0)
        spdlog::debug("[XXX SM] 音效: {}", event.substr(11));

    // ── spawn_vfx:slash ── 斩击特效
    if (event == "spawn_vfx:slash")
    {
        // TODO: vfxSys->emit("slash", actorPos, facingSign);
    }

    // ── shake_screen ── 屏幕震动（终结技）
    if (event == "shake_screen")
    {
        // TODO: cameraSys->shake(0.25f, 6.0f);
    }
});
```

---

### 2.4 registerCondition — 自定义条件（可选）

```cpp
sm.registerCondition("IS_LOW_HP", [actor](const engine::statemachine::StateController&) -> bool {
    auto* attr = actor ? actor->getComponent<game::component::AttributeComponent>() : nullptr;
    return attr && (attr->getHp() < attr->getMaxHp() * 0.25f);
});
// 在 .sm.json 的某条 Transition.trigger 字段填写 "IS_LOW_HP" 即可生效
```

---

### 2.5 forceTransition — 外部强制跳转

```cpp
sm.forceTransition("HURT");    // 被击中时由外部系统调用
sm.forceTransition("DEAD");    // 角色死亡
sm.forceTransition("ATTACK_3"); // 调试用：直接跳到第 3 段攻击
```

---

## 三、帧事件约定表

| 事件字符串 | 含义 | 默认处理方 |
|---|---|---|
| `play_sound:xxx` | 播放 `assets/audio/xxx.mp3` | `tickPlayerSM` 统一处理 |
| `spawn_hitbox:N` | 生成近战判定盒，N=连招段(1–4) | `xxx_sm.cpp` → TODO 战斗系统 |
| `spawn_vfx:slash` | 斩击特效 | `xxx_sm.cpp` → TODO VFX 系统 |
| `shake_screen` | 屏幕震动（终结技） | `xxx_sm.cpp` → TODO 摄像机 |
| `spawn_arrow` | 生成投射物 | `xxx_sm.cpp` → TODO 投射物系统 |
| `enable_hitbox` | 开启常驻判定盒（多帧连续） | `xxx_sm.cpp` 自定义 |
| `disable_hitbox` | 关闭常驻判定盒 | `xxx_sm.cpp` 自定义 |

自定义新事件：在 `.sm.json` 的 `frame_events` 里填入任意字符串，在 `setOnFrameEvent` 回调里增加对应分支即可。

---

## 四、朝向翻转机制

**统一由 `tickPlayerSM()` 处理，`xxx_sm.cpp` 无需重复操作。**

```
moveL 按下 → controller->setFacingDirection(Left)  + sprite->setFlipped(true)
moveR 按下 → controller->setFacingDirection(Right) + sprite->setFlipped(false)
ATTACK 期间跳过翻转（防止出手瞬间换向）
```

帧事件回调中通过 `ctrl->getFacingDirection()` 获取当前朝向，用于计算判定盒偏移方向。

---

## 五、连击窗口（ComboWindow）原理

`.sm.json` 中 `StateNode.windows` 配置：

```json
"windows": [
  { "start": 3, "end": 6, "type": 1 }
]
```

`type` 枚举：`0` = Locked（锁定）、`1` = ComboWindow（连招窗口）、`2` = Cancelable（可取消）

运行时流程：
1. 玩家在 Locked 区间按下攻击键 → `sm.pushInput("KEY_ATTACK", time)` 存入 0.2s 缓冲池
2. 动画播到帧 3，进入 ComboWindow
3. `update()` 检测缓冲池有 `KEY_ATTACK`，满足 `requireWindow=true` 的 Transition → 跳转到下一段
4. 若 ComboWindow 结束前缓冲已过期或未按 → `ANIM_END` → `IDLE`（连招自然断开）

**无需在 `xxx_sm.cpp` 里手动管理连击计数。**

---

## 六、新增角色 Checklist

```
□ 1. 创建 src/game/statemachine/xxx_sm.cpp
       实现 static void setupXxxSM(StateController&, GameObject*, Context&)
       在文件底部 setupCharacterSM() 的 if 链末尾添加分支

□ 2. CMakeLists.txt 添加一行：
       src/game/statemachine/xxx_sm.cpp

□ 3. 创建 assets/textures/Actors/Xxx.sm.json
       character_id 须与代码中的字符串完全一致

□ 4. 帧编辑器中为角色创建动作
       animationId 须与 JSON 中 animation_id 字段一致

□ 5. 角色编辑器中填写 smPath 字段
       指向上一步创建的 .sm.json 文件路径

□ 6. 实体管理面板：生成角色 → 勾选"设为控制对象"
       自动触发 loadPlayerSM() → setupCharacterSM() 完成接入
```

---

## 七、tickPlayerSM 每帧工作流（参考）

```
① 查询物理状态（grounded / land / vel）
② 构建持续型 activeInputs
     GROUNDED / AIRBORNE / RISING / FALLING / LAND
     IS_MOVING / NO_INPUT / KEY_MOVE_L / KEY_MOVE_R
     IS_ATTACKING / IS_DASHING
③ 朝向更新（moveL/moveR → setFacingDirection + setFlipped，ATTACK 期间跳过）
④ 瞬间型输入 → sm.pushInput("KEY_ATTACK", time)（0.2s 缓冲）
⑤ sm.update(dt, activeInputs, time) → UpdateResult
⑥ 应用根位移（result.rootMotionDx * facingSign → physics->applyImpulse）
⑦ 处理帧事件（result.firedEvents）
     play_sound:xxx  → AudioMixer 播放
     spawn_hitbox:N  → xxx_sm.cpp 的 setOnFrameEvent 回调
     spawn_vfx / shake_screen → xxx_sm.cpp 的 setOnFrameEvent 回调
⑧ 状态变化时同步动画（anim->play(stateNode.animationId)）
```
