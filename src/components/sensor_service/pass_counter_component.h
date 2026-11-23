#if !defined(PASS_COUNTER_COMPONENT_H)
#define PASS_COUNTER_COMPONENT_H

#include "digital_sensor_component.h"
#include <functional>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/**
 * @brief Counts objects (fish) passing an IR sensor using both signal edges.
 *
 * A pass is one full cycle: line goes ACTIVE (object enters the beam) and later
 * returns INACTIVE (object has left). Noise handling:
 *  - Every edge restarts a settle timer; the level is only evaluated once the
 *    line has been quiet for `settleMs`. Spikes shorter than that are ignored,
 *    and short gaps inside one fish (fins, tail flick, bubbles) are merged.
 *  - A blocked period shorter than `minBlockMs` is treated as a glitch.
 *
 * The pass callback runs in the system work queue (thread context, not ISR).
 */
class PassCounterComponent
{
public:
    struct Config
    {
        uint32_t settleMs = 8;     ///< Line must be stable this long before its level is trusted
        uint32_t minBlockMs = 15; ///< Shorter blockages are noise
    };

    explicit PassCounterComponent(const gpio_dt_spec *pin);

    /**
     * @brief Configure the pin and register the pass callback. Interrupts stay disabled.
     * @retval 0 on success, negative errno otherwise.
     */
    int beginInterrupt(std::function<void()> onPass);
    int beginInterrupt(std::function<void()> onPass, const Config &config);

    /** @brief Start detecting. The current line level becomes the initial state. */
    int enableInterrupt();

    /** @brief Stop detecting and drop any half-seen pass. */
    int disableInterrupt();

    /** @brief Blockages rejected as noise since the last enable (diagnostics). */
    uint32_t rejectedCount() const { return static_cast<uint32_t>(atomic_get(&_rejected)); }

private:
    struct WorkCtx
    {
        k_work_delayable work; // must stay first: handler casts back from it
        PassCounterComponent *owner;
    };

    DigitalSensorComponent _sensor;
    Config _config;
    std::function<void()> _onPass;
    WorkCtx _workCtx;

    atomic_t _edgePending = ATOMIC_INIT(0); // an edge arrived since the last settled read
    int64_t _firstEdgeMs = 0;               // time of the first edge of the pending transition
    bool _blocked = false;                  // last settled state
    int64_t _blockStartMs = 0;
    atomic_t _rejected = ATOMIC_INIT(0);

    void onEdge();      // ISR context
    void onSettled();   // work queue context
    static void workHandler(k_work *work);
};

#endif // PASS_COUNTER_COMPONENT_H
