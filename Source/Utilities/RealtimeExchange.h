#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>

namespace nedd
{
/**
    Hands immutable heap objects from the message thread to the audio thread without locks
    and without the audio thread ever freeing memory.

    - The message thread calls publish() with a freshly built object.
    - The audio thread calls acquire() once per block and uses the returned pointer until the
      next acquire(). Objects replaced by a newer one are pushed onto a retire queue.
    - The message thread frees retired objects in collectGarbage(), which publish() also calls.

    Each publish() produces at most one retired object, and publish() drains the retire queue
    first, so a small fixed queue can never overflow in practice.
*/
template <typename T>
class RealtimeExchange
{
public:
    RealtimeExchange() = default;

    ~RealtimeExchange()
    {
        delete pending.exchange (nullptr);
        delete active;
        collectGarbage();
    }

    /** Any non-audio thread (writers are serialised with a spin lock the audio thread never takes). */
    void publish (std::unique_ptr<T> object)
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        collectGarbageLocked();
        delete pending.exchange (object.release());
    }

    /** Any non-audio thread. Frees objects the audio thread no longer references. */
    void collectGarbage()
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        collectGarbageLocked();
    }

    /** Audio thread. Returns the newest published object (may be nullptr before the first publish). */
    const T* acquire() noexcept
    {
        if (T* incoming = pending.exchange (nullptr))
        {
            if (active != nullptr)
            {
                const auto scope = retired.write (1);

                if (scope.blockSize1 > 0)
                {
                    retiredSlots[(size_t) scope.startIndex1] = active;
                }
                else
                {
                    // Retire queue full: keep the current object and drop the newcomer back into
                    // the pending slot so the message thread can reclaim it. Never happens with
                    // the publish/collect protocol above, but stays leak- and crash-free if it does.
                    jassertfalse;
                    T* expected = nullptr;
                    if (! pending.compare_exchange_strong (expected, incoming))
                        retiredOverflow.store (incoming);
                    return active;
                }
            }

            active = incoming;
        }

        return active;
    }

    /** Audio thread (or when the audio thread is stopped): the object currently in use. */
    const T* current() const noexcept { return active; }

private:
    void collectGarbageLocked()
    {
        const auto scope = retired.read (retired.getNumReady());

        for (int i = 0; i < scope.blockSize1; ++i) delete retiredSlots[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i) delete retiredSlots[(size_t) (scope.startIndex2 + i)];

        delete retiredOverflow.exchange (nullptr);
    }

    static constexpr int kRetireCapacity = 16;
    juce::SpinLock writerLock;

    std::atomic<T*> pending { nullptr };
    std::atomic<T*> retiredOverflow { nullptr };
    T* active = nullptr;

    juce::AbstractFifo retired { kRetireCapacity };
    std::array<T*, (size_t) kRetireCapacity> retiredSlots {};

    JUCE_DECLARE_NON_COPYABLE (RealtimeExchange)
};

} // namespace nedd
