#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace nedd
{
/** Fixed-capacity single-producer / single-consumer queue for trivially copyable items. */
template <typename T, int Capacity>
class SpscFifo
{
public:
    static_assert (std::is_trivially_copyable_v<T>, "SpscFifo only carries plain data");

    /** Producer thread. Returns false (and drops the item) when full. */
    bool push (const T& item) noexcept
    {
        const auto scope = fifo.write (1);

        if (scope.blockSize1 > 0)
        {
            items[(size_t) scope.startIndex1] = item;
            return true;
        }

        if (scope.blockSize2 > 0)
        {
            items[(size_t) scope.startIndex2] = item;
            return true;
        }

        return false;
    }

    /** Consumer thread. */
    bool pop (T& item) noexcept
    {
        const auto scope = fifo.read (1);

        if (scope.blockSize1 > 0)
        {
            item = items[(size_t) scope.startIndex1];
            return true;
        }

        if (scope.blockSize2 > 0)
        {
            item = items[(size_t) scope.startIndex2];
            return true;
        }

        return false;
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }
    void clear() noexcept { fifo.reset(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> items {};
};

} // namespace nedd
