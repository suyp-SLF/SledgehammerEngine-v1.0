/**
 * input_buffer.h  —  指令缓冲池
 *
 * 存储过去 windowSeconds（默认 0.2s）内的按键输入。
 * 当动作进入连招/可取消区间时，优先消耗缓冲池中的指令，
 * 实现"提前按键也能触发连招"的手感。
 */
#pragma once

#include <string>
#include <deque>

namespace engine::statemachine {

class InputBuffer {
public:
    float windowSeconds = 0.20f;  // 指令缓冲窗口（秒）

    /** 注册一次按键输入（通常在按键按下时调用）*/
    void push(const std::string& action, float currentTime);

    /** 每帧调用，清除过期条目 */
    void update(float currentTime);

    /** 检查并消耗缓冲池中最早一条匹配指令，成功返回 true。*/
    bool consume(const std::string& action, float currentTime);

    /** 仅检查缓冲池中是否有匹配指令（不消耗）*/
    bool has(const std::string& action, float currentTime) const;

    /** 清空所有缓冲 */
    void clear();

    int size() const { return static_cast<int>(m_buffer.size()); }

private:
    struct Entry {
        std::string action;
        float       timestamp = 0.0f;
        bool        consumed  = false;
    };
    std::deque<Entry> m_buffer;
};

} // namespace engine::statemachine
