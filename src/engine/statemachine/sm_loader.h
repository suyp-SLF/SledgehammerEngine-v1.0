/**
 * sm_loader.h  —  状态机数据 JSON 序列化/反序列化（引擎层）
 *
 * 文件格式：*.sm.json
 * 无游戏业务依赖，可在任意场景中使用。
 */
#pragma once

#include "sm_types.h"
#include <string>

namespace engine::statemachine {

class SmLoader {
public:
    /** 将 StateMachineData 序列化为 JSON 文件，返回 true 表示成功 */
    static bool save(const StateMachineData& data, const std::string& path);

    /** 从 JSON 文件加载 StateMachineData，返回 true 表示成功 */
    static bool load(const std::string& path, StateMachineData& outData);

    /** 检查当前是否有错误消息 */
    static const std::string& lastError() { return s_lastError; }

private:
    static std::string s_lastError;
};

} // namespace engine::statemachine
