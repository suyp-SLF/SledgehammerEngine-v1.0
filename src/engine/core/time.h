#pragma once
#include <SDL3/SDL_stdinc.h>

namespace engine::core
{
    /**
     * @brief 时间管理类，用于处理游戏中的时间相关操作，如帧率控制、时间缩放等
     */
    class Time final
    {
    private:
        Uint64 _last_time = 0;        // 上一次更新的时间戳，用于计算时间差
        Uint64 _frame_start_time = 0; // 当前帧开始的时间戳，用于帧率限制
        double _delta_time = 0.0;     // 经过的时间（考虑时间缩放）
        double _timer_scale = 1.0f;   // 时间缩放因子，1.0为正常速度，小于1.0为减速，大于1.0为加速

        int _target_fps = 0;             // 目标帧率，0表示不限制帧率
        double _target_frame_time = 0.0; // 目标帧时间（毫秒），根据目标帧率计算得出
        bool _frame_limit_enabled = true;

    public:
        Time();                            // 构造函数，初始化时间管理器
        void update();                     // 更新时间状态，计算时间差等
        float getDeltaTime() const;        // 获取经过的时间（考虑时间缩放）
        float getUnscaleDeltaTime() const; // 获取原始时间差（未考虑时间缩放）
        float getTimerScale() const;       // 获取当前时间缩放因子
        void setTimerScale(float scale);   // 设置时间缩放因子
        void setTargetFPS(int fps);        // 设置目标帧率
        void setFrameLimitEnabled(bool enabled);
        int getTargetFPS() const;          // 获取当前设置的目标帧率

    private:
        void limitFrameRate(double current_delta_time); // 限制帧率，确保达到目标帧率
    };
}; // namespace engine::core