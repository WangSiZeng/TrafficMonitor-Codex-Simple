#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace codex_usage {

// One visible column per query interval, anchored at plugin initialization.
// Only successful queries record samples; painting never changes the history.
// Raw used percentages allow remaining/used changes to reinterpret old samples.
class GraphHistory {
public:
    struct Sample { ULONGLONG slot; double used_percent; };
    static constexpr size_t capacity = 4096;

    void Reset(ULONGLONG origin, DWORD interval) {
        AcquireSRWLockExclusive(&lock_);
        samples_.clear();
        origin_ = origin;
        interval_ = (std::max)(1UL, interval);
        ReleaseSRWLockExclusive(&lock_);
    }

    void Record(ULONGLONG milliseconds, double used_percent) {
        if (!std::isfinite(used_percent) || used_percent < 0 || used_percent > 100) return;
        AcquireSRWLockExclusive(&lock_);
        if (milliseconds < origin_) { ReleaseSRWLockExclusive(&lock_); return; }
        const ULONGLONG slot = (milliseconds - origin_) / interval_;
        if (!samples_.empty() && slot < samples_.back().slot) { ReleaseSRWLockExclusive(&lock_); return; }
        if (!samples_.empty() && slot == samples_.back().slot) samples_.back().used_percent = used_percent;
        else {
            try {
                if (samples_.size() == capacity) samples_.pop_front();
                samples_.push_back(Sample{slot, used_percent});
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
        if (!dc || rect.right <= rect.left || rect.bottom <= rect.top) return;
        AcquireSRWLockShared(&lock_);
        if (milliseconds < origin_) { ReleaseSRWLockShared(&lock_); return; }
        const ULONGLONG current_slot = (milliseconds - origin_) / interval_;
        for (const auto& sample : samples_) {
            if (sample.slot > current_slot) continue;
            const ULONGLONG age = current_slot - sample.slot;
            if (age >= static_cast<ULONGLONG>(rect.right - rect.left)) continue;
            const double percent = remaining ? 100.0 - sample.used_percent : sample.used_percent;
            const int height = static_cast<int>((rect.bottom - rect.top) * percent / 100.0);
            if (height <= 0) continue;
            const int x = rect.right - 1 - static_cast<int>(age);
            RECT column{x, rect.bottom - height, x + 1, rect.bottom};
            FillRect(dc, &column, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        ReleaseSRWLockShared(&lock_);
    }
private:
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    ULONGLONG origin_ = 0;
    DWORD interval_ = 300000;
    std::deque<Sample> samples_;
};
}
