#include "time.h"
#include <SDL3/SDL_timer.h>
#include <spdlog/spdlog.h>

namespace engine::core
{
    /**
     * Time类的默认构造函数
     * 初始化_last_time和_frame_start_time为当前系统时间
     */
    Time::Time()
    {
        // 获取当前系统时间（纳秒级）并赋值给_last_time和_frame_start_time
        _last_time = SDL_GetTicksNS();
        _frame_start_time = _last_time;

        // 使用spdlog记录trace级别的日志，输出初始化时的当前时间
        spdlog::trace("Time 初始化，当前时间：{}", _last_time);
    }

    /**
     * 更新时间状态，计算帧时间差并控制帧率
     * 该方法在每一帧开始时调用，用于更新游戏的时间相关数据
     */
    void Time::update()
    {
        _frame_start_time = SDL_GetTicksNS();
        // 实际经过的纳秒
        Uint64 frame_ns = _frame_start_time - _last_time;
        double current_delta = static_cast<double>(frame_ns) / 1e9;

        if (_frame_limit_enabled && _target_frame_time > 0 && current_delta < _target_frame_time)
        {
            double time_to_wait = _target_frame_time - current_delta;
            SDL_DelayNS(static_cast<Uint64>(time_to_wait * 1e9));

            // 重新计算补偿后的 delta
            _delta_time = static_cast<float>(SDL_GetTicksNS() - _last_time) / 1e9f;
        }
        else
        {
            _delta_time = static_cast<float>(current_delta);
        }

        _last_time = SDL_GetTicksNS();
    }

    /**
     * 获取经过时间缩放后的增量时间
     * @return 返回经过时间缩放系数调整后的增量时间值
     */
    float Time::getDeltaTime() const
    {
        return _delta_time * _timer_scale; // 返回原始增量时间与时间缩放系数的乘积
    }

    /**
     * 获取未缩放的时间增量（delta time）
     * @return 返回自上一帧以来的时间增量（单位：秒），不受时间缩放影响
     */
    float Time::getUnscaleDeltaTime() const
    {
        return _delta_time;
    }

    /**
     * 获取计时器的时间缩放比例
     * @return 返回计时器的时间缩放比例值
     */
    float Time::getTimerScale() const
    {
        return _timer_scale; // 返回成员变量_timer_scale的值
    }

    /**
     * 设置时间缩放比例
     * @param scale 时间缩放比例值，用于调整游戏内时间的流逝速度
     */
    void Time::setTimerScale(float scale)
    {                         // 设置时间缩放比例的函数
        _timer_scale = scale; // 将传入的缩放比例值赋给成员变量_timer_scale
    }

    /**
     * 设置目标帧率
     * @param fps 目标帧率值，单位为帧每秒(FPS)
     */
    void Time::setTargetFPS(int fps)
    {
        spdlog::info("设置目标帧率：{}", fps);
        _target_fps = fps;
        if (fps > 0)
        {
            _target_frame_time = 1.0 / static_cast<double>(fps);
        }
        else
        {
            _target_frame_time = 0;
        }
    }

    void Time::setFrameLimitEnabled(bool enabled)
    {
        _frame_limit_enabled = enabled;
    }

    /**
     * 获取目标帧率的方法
     * @return 返回目标帧率值_target_fps
     */
    int Time::getTargetFPS() const
    {
        return _target_fps; // 返回成员变量_target_fps的值
    }
    /**
     * 限制帧率函数，确保渲染帧率不超过目标帧率
     * @param current_delta_time 当前帧的增量时间（秒）
     */
    void Time::limitFrameRate(double current_delta_time) // 建议用double保持精度
    {
        // 如果实际耗时小于目标耗时，说明跑快了，需要等待
        if (current_delta_time < _target_frame_time)
        {
            double time_to_wait = _target_frame_time - current_delta_time;
            Uint64 ns_to_wait = static_cast<Uint64>(time_to_wait * 1000000000.0);
            SDL_DelayNS(ns_to_wait);
        }

        // 无论是否延迟，最终更新 delta_time 为从上一帧结束到现在实际经过的时间
        _delta_time = static_cast<float>(SDL_GetTicksNS() - _last_time) / 1e9f;
    }
}
