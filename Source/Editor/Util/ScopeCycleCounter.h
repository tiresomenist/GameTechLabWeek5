#pragma once
#include <chrono>

class FScopeCycleCounter
{
public:
    FScopeCycleCounter()
        : StartTime(Clock::now())
    {
    }

    double Finish() const
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - StartTime).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point StartTime;    //시작 시간
};