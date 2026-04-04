#include "sm_loader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

namespace engine::statemachine {

std::string SmLoader::s_lastError;

// ─────────────────────────────────────────────────────────────────────────────
//  保存
// ─────────────────────────────────────────────────────────────────────────────
bool SmLoader::save(const StateMachineData& data, const std::string& path)
{
    using json = nlohmann::json;
    json root;
    root["character_id"]   = data.characterId;
    root["initial_state"]  = data.initialState;
    root["states"]         = json::object();

    for (const auto& [stateName, node] : data.states)
    {
        json jn;
        jn["animation_id"] = node.animationId;
        jn["loop"]         = node.loop;
        jn["total_frames"] = node.totalFrames;

        // 转换条件
        jn["transitions"] = json::array();
        for (const auto& t : node.transitions)
        {
            json jt;
            jt["trigger"]        = t.trigger;
            jt["target"]         = t.targetState;
            jt["priority"]       = t.priority;
            jt["require_window"] = t.requireWindow;
            jt["window_type"]    = static_cast<int>(t.windowType);
            jn["transitions"].push_back(jt);
        }

        // 帧区间
        jn["windows"] = json::array();
        for (const auto& w : node.windows)
        {
            json jw;
            jw["start"]     = w.startFrame;
            jw["end"]       = w.endFrame;
            jw["type"]      = static_cast<int>(w.type);
            jn["windows"].push_back(jw);
        }

        // 帧事件
        jn["frame_events"] = json::array();
        for (const auto& fe : node.frameEvents)
        {
            json jfe;
            jfe["frame"] = fe.frame;
            jfe["event"] = fe.event;
            jn["frame_events"].push_back(jfe);
        }

        // 根位移
        jn["root_motion"] = json::array();
        for (const auto& rm : node.rootMotion)
        {
            json jrm;
            jrm["frame"] = rm.frame;
            jrm["dx"]    = rm.dx;
            jrm["dy"]    = rm.dy;
            jn["root_motion"].push_back(jrm);
        }

        root["states"][stateName] = jn;
    }

    std::ofstream ofs(path);
    if (!ofs)
    {
        s_lastError = "无法写入文件: " + path;
        spdlog::error("[SmLoader] 保存失败: {}", path);
        return false;
    }
    ofs << root.dump(2);
    spdlog::info("[SmLoader] 保存成功: {} ({} 个状态)",
                 path, data.states.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  加载
// ─────────────────────────────────────────────────────────────────────────────
bool SmLoader::load(const std::string& path, StateMachineData& outData)
{
    using json = nlohmann::json;
    std::ifstream ifs(path);
    if (!ifs)
    {
        s_lastError = "文件不存在: " + path;
        return false;
    }

    json root;
    try { ifs >> root; }
    catch (const std::exception& e)
    {
        s_lastError = std::string("JSON 解析失败: ") + e.what();
        spdlog::error("[SmLoader] {}", s_lastError);
        return false;
    }

    outData.characterId  = root.value("character_id",  "");
    outData.initialState = root.value("initial_state", "IDLE");
    outData.states.clear();

    // 必须先存入局部变量，不能在 range-for 的 range 表达式里链式调用
    // root.value(...).items()：value() 返回临时对象，items() 只持有引用，
    // 临时对象会在 range-for 初始化完成后销毁 → 悬空引用 → EXC_BAD_ACCESS
    if (!root.contains("states") || !root["states"].is_object())
    {
        spdlog::warn("[SmLoader] JSON 中缺少 'states' 对象字段: {}", path);
        return true;   // 空状态机也算合法
    }
    const json& statesJson = root["states"];

    for (auto& [stateName, jn] : statesJson.items())
    {
        StateNode node;
        node.animationId  = jn.value("animation_id", stateName);
        node.loop         = jn.value("loop", true);
        node.totalFrames  = jn.value("total_frames", 8);

        for (const auto& jt : jn.value("transitions", json::array()))
        {
            Transition t;
            t.trigger       = jt.value("trigger", "");
            t.targetState   = jt.value("target", "");
            t.priority      = jt.value("priority", 0);
            t.requireWindow = jt.value("require_window", false);
            t.windowType    = static_cast<WindowType>(jt.value("window_type", 0));
            node.transitions.push_back(t);
        }

        for (const auto& jw : jn.value("windows", json::array()))
        {
            FrameWindow w;
            w.startFrame = jw.value("start", 0);
            w.endFrame   = jw.value("end", 0);
            w.type       = static_cast<WindowType>(jw.value("type", 0));
            node.windows.push_back(w);
        }

        for (const auto& jfe : jn.value("frame_events", json::array()))
        {
            FrameEventData fe;
            fe.frame = jfe.value("frame", 0);
            fe.event = jfe.value("event", "");
            node.frameEvents.push_back(fe);
        }

        for (const auto& jrm : jn.value("root_motion", json::array()))
        {
            RootMotionFrame rm;
            rm.frame = jrm.value("frame", 0);
            rm.dx    = jrm.value("dx", 0.0f);
            rm.dy    = jrm.value("dy", 0.0f);
            node.rootMotion.push_back(rm);
        }

        outData.states[stateName] = std::move(node);
    }

    spdlog::info("[SmLoader] 加载成功: {} ({} 个状态)",
                 path, outData.states.size());
    return true;
}

} // namespace engine::statemachine
