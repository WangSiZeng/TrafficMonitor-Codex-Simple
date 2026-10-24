#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace codex_usage {

// One visible column per second. Samples are raw used percentages, so changing
// the remaining/used logic also updates the already collected history.
class GraphHistory {
public:
    struct Sample { ULONGLONG second; double used_percent; };
    static constexpr size_t capacity = 4096;

    void Record(ULONGLONG milliseconds, double used_percent) {
        if (!std::isfinite(used_percent) || used_percent < 0 || used_percent > 100) return;
        const ULONGLONG second = milliseconds / 1000;
        AcquireSRWLockExclusive(&lock_);
        if (!samples_.empty() && second < samples_.back().second) samples_.clear();
        if (!samples_.empty() && second == samples_.back().second) samples_.back().used_percent = used_percent;
        else {
            try {
                if (samples_.size() == capacity) samples_.pop_front();
                samples_.push_back(Sample{second, used_percent});
            }
            catch (...) { ReleaseSRWLockExclusive(&lock_); throw; }
        }
        ReleaseSRWLockExclusive(&lock_);
    }

    std::vector<Sample> Snapshot() const {
        AcquireSRWLockShared(&lock_);
        std::vector<Sample> result;
        try { result.assign(samples_.begin(), samples_.end()); }
        catch (...) { ReleaseSRWLockShared(&lock_); throw; }
        ReleaseSRWLockShared(&lock_);
        return result;
    }

    void Draw(HDC dc, const RECT& rect, ULONGLONG milliseconds, bool remaining) const {
        const ULONGLONG current_second = milliseconds / 1000;
        const auto samples = Snapshot();
        for (const auto& sample : samples) {
            if (sample.second > current_second) continue;
            const ULONGLONG age = current_second - sample.second;
            if (age >= static_cast<ULONGLONG>(rect.right - rect.left)) continue;
            const double percent = remaining ? 100.0 - sample.used_percent : sample.used_percent;
            const int height = static_cast<int>((rect.bottom - rect.top) * percent / 100.0);
            if (height <= 0) continue;
            const int x = rect.right - 1 - static_cast<int>(age);
            RECT column{x, rect.bottom - height, x + 1, rect.bottom};
            FillRect(dc, &column, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
    }
private:
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::deque<Sample> samples_;
};
}
