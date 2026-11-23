#include "pass_counter_component.h"

PassCounterComponent::PassCounterComponent(const gpio_dt_spec *pin) : _sensor(pin)
{
}

int PassCounterComponent::beginInterrupt(std::function<void()> onPass)
{
    return beginInterrupt(std::move(onPass), Config{});
}

int PassCounterComponent::beginInterrupt(std::function<void()> onPass, const Config &config)
{
    _config = config;
    _onPass = std::move(onPass);
    _workCtx.owner = this;
    k_work_init_delayable(&_workCtx.work, &PassCounterComponent::workHandler);

    int ret = _sensor.beginInterrupt([this]() { onEdge(); });
    if (ret != 0)
        return ret;

    return _sensor.disableInterrupt();
}

int PassCounterComponent::enableInterrupt()
{
    k_work_cancel_delayable(&_workCtx.work);
    atomic_clear(&_edgePending);
    atomic_clear(&_rejected);

    // A fish already in the beam at start is counted when it leaves.
    _blocked = _sensor.state();
    _blockStartMs = k_uptime_get();

    return _sensor.enableInterrupt(GPIO_INT_EDGE_BOTH);
}

int PassCounterComponent::disableInterrupt()
{
    int ret = _sensor.disableInterrupt();
    k_work_cancel_delayable_sync(&_workCtx.work, nullptr);
    atomic_clear(&_edgePending);
    _blocked = false;
    return ret;
}

void PassCounterComponent::onEdge()
{
    // First edge of a new transition: remember when it started, so durations
    // measure the real blockage and not the settle delay.
    if (atomic_cas(&_edgePending, 0, 1))
        _firstEdgeMs = k_uptime_get();

    // Each further edge pushes the evaluation back: we only read once the line is quiet.
    k_work_reschedule(&_workCtx.work, K_MSEC(_config.settleMs));
}

void PassCounterComponent::onSettled()
{
    // Read the timestamp and re-arm atomically w.r.t. the ISR.
    const unsigned int key = irq_lock();
    const int64_t edgeMs = _firstEdgeMs;
    atomic_clear(&_edgePending);
    irq_unlock(key);

    const bool level = _sensor.state();
    if (level == _blocked)
        return; // spike(s) that returned to the same level: noise

    if (level)
    {
        // Clear -> blocked: object entered the beam
        _blocked = true;
        _blockStartMs = edgeMs;
        return;
    }

    // Blocked -> clear: object left. Decide whether it was a real fish.
    _blocked = false;
    const int64_t blockedMs = edgeMs - _blockStartMs;
    if (blockedMs < _config.minBlockMs)
    {
        atomic_inc(&_rejected);
        return;
    }

    if (_onPass)
        _onPass();
}

void PassCounterComponent::workHandler(k_work *work)
{
    k_work_delayable *dwork = k_work_delayable_from_work(work);
    auto *ctx = reinterpret_cast<WorkCtx *>(dwork);
    ctx->owner->onSettled();
}
