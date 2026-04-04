#include "input_buffer.h"
#include <algorithm>

namespace engine::statemachine {

void InputBuffer::push(const std::string& action, float currentTime)
{
    // 避免同一帧重复推入同一指令
    if (!m_buffer.empty())
    {
        auto& back = m_buffer.back();
        if (back.action == action && (currentTime - back.timestamp) < 0.016f)
            return;
    }
    m_buffer.push_back({ action, currentTime, false });
}

void InputBuffer::update(float currentTime)
{
    // 移除已消耗或过期的条目
    float cutoff = currentTime - windowSeconds;
    while (!m_buffer.empty())
    {
        auto& front = m_buffer.front();
        if (front.consumed || front.timestamp < cutoff)
            m_buffer.pop_front();
        else
            break;
    }
}

bool InputBuffer::consume(const std::string& action, float currentTime)
{
    float cutoff = currentTime - windowSeconds;
    for (auto& e : m_buffer)
    {
        if (!e.consumed && e.timestamp >= cutoff && e.action == action)
        {
            e.consumed = true;
            return true;
        }
    }
    return false;
}

bool InputBuffer::has(const std::string& action, float currentTime) const
{
    float cutoff = currentTime - windowSeconds;
    for (const auto& e : m_buffer)
    {
        if (!e.consumed && e.timestamp >= cutoff && e.action == action)
            return true;
    }
    return false;
}

void InputBuffer::clear()
{
    m_buffer.clear();
}

} // namespace engine::statemachine
