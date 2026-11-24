/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vibratorfeature-wrapper"

#include "Vibrator.h"

#include <android-base/logging.h>

#include <chrono>

namespace aidl::android::hardware::vibrator {

namespace {

struct PrimitiveEffect {
    CompositePrimitive primitive;
    // Effect id of the stock HAL (odm/firmware/<id>_*_RTP.bin); -1 plays nothing
    int32_t effectId;
    // Length of the stock waveform (effect<id>.bin, 16 bit at 48 kHz)
    int32_t durationMs;
};

// Short, crisp HyperOS effects for the ticks and clicks of the system UI (slider textures,
// confirmations, fingerprint unlock); longer ones for the swells.
constexpr PrimitiveEffect kPrimitiveEffects[] = {
        {CompositePrimitive::NOOP, -1, 0},
        {CompositePrimitive::CLICK, 0, 16},          // 0_click
        {CompositePrimitive::THUD, 3, 15},           // 3_thud
        {CompositePrimitive::SPIN, 170, 196},        // 170_slidingDamping
        {CompositePrimitive::QUICK_RISE, 57, 215},   // 57_FadeIn
        {CompositePrimitive::SLOW_RISE, 57, 215},    // 57_FadeIn
        {CompositePrimitive::QUICK_FALL, 166, 14},   // 166_feedbackNeutral (no short fall)
        {CompositePrimitive::LIGHT_TICK, 183, 12},   // 183_signalList
        {CompositePrimitive::LOW_TICK, 181, 13},     // 181_signalKeyUnit
};

constexpr int32_t kCompositionDelayMaxMs = 1000;
constexpr int32_t kCompositionSizeMax = 32;

const PrimitiveEffect* findPrimitive(CompositePrimitive primitive) {
    for (const auto& entry : kPrimitiveEffects) {
        if (entry.primitive == primitive) return &entry;
    }
    return nullptr;
}

// The stock HAL takes three strengths instead of a scale. Only full-scale primitives play
// STRONG: the 0.7 clicks of the system UI (gesture thresholds, high-emphasis taps) are
// subtler at MEDIUM.
EffectStrength scaleToStrength(float scale) {
    if (scale <= 0.4f) return EffectStrength::LIGHT;
    if (scale <= 0.8f) return EffectStrength::MEDIUM;
    return EffectStrength::STRONG;
}

ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace

Vibrator::Vibrator(std::shared_ptr<IVibrator> hal) : mHal(std::move(hal)) {}

Vibrator::~Vibrator() {
    stopComposition();
}

ndk::ScopedAStatus Vibrator::getCapabilities(int32_t* _aidl_return) {
    auto status = mHal->getCapabilities(_aidl_return);
    if (status.isOk()) *_aidl_return |= IVibrator::CAP_COMPOSE_EFFECTS;
    return status;
}

ndk::ScopedAStatus Vibrator::off() {
    stopComposition();
    return mHal->off();
}

ndk::ScopedAStatus Vibrator::on(int32_t timeoutMs,
                                const std::shared_ptr<IVibratorCallback>& callback) {
    stopComposition();
    return mHal->on(timeoutMs, callback);
}

ndk::ScopedAStatus Vibrator::perform(Effect effect, EffectStrength strength,
                                     const std::shared_ptr<IVibratorCallback>& callback,
                                     int32_t* _aidl_return) {
    stopComposition();
    return mHal->perform(effect, strength, callback, _aidl_return);
}

ndk::ScopedAStatus Vibrator::getSupportedEffects(std::vector<Effect>* _aidl_return) {
    return mHal->getSupportedEffects(_aidl_return);
}

ndk::ScopedAStatus Vibrator::setAmplitude(float amplitude) {
    return mHal->setAmplitude(amplitude);
}

ndk::ScopedAStatus Vibrator::setExternalControl(bool enabled) {
    return mHal->setExternalControl(enabled);
}

ndk::ScopedAStatus Vibrator::getCompositionDelayMax(int32_t* _aidl_return) {
    *_aidl_return = kCompositionDelayMaxMs;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCompositionSizeMax(int32_t* _aidl_return) {
    *_aidl_return = kCompositionSizeMax;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getSupportedPrimitives(std::vector<CompositePrimitive>* _aidl_return) {
    _aidl_return->clear();
    for (const auto& entry : kPrimitiveEffects) _aidl_return->push_back(entry.primitive);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getPrimitiveDuration(CompositePrimitive primitive,
                                                  int32_t* _aidl_return) {
    const PrimitiveEffect* entry = findPrimitive(primitive);
    if (entry == nullptr) return unsupported();
    *_aidl_return = entry->durationMs;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::compose(const std::vector<CompositeEffect>& composite,
                                     const std::shared_ptr<IVibratorCallback>& callback) {
    if (composite.empty() || composite.size() > kCompositionSizeMax) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    for (const auto& e : composite) {
        if (e.delayMs < 0 || e.delayMs > kCompositionDelayMaxMs || e.scale < 0.0f ||
            e.scale > 1.0f) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        if (findPrimitive(e.primitive) == nullptr) return unsupported();
    }

    stopComposition();
    std::lock_guard<std::mutex> lock(mLock);
    mStop = false;
    mThread = std::thread(&Vibrator::playComposition, this, composite, callback);
    return ndk::ScopedAStatus::ok();
}

void Vibrator::playComposition(std::vector<CompositeEffect> composite,
                               std::shared_ptr<IVibratorCallback> callback) {
    for (const auto& e : composite) {
        if (!waitFor(e.delayMs)) return;
        const PrimitiveEffect* entry = findPrimitive(e.primitive);
        if (entry->effectId < 0 || e.scale == 0.0f) continue;

        int32_t durationMs = 0;
        auto status = mHal->perform(static_cast<Effect>(entry->effectId),
                                    scaleToStrength(e.scale), nullptr, &durationMs);
        if (!status.isOk()) {
            LOG(WARNING) << "perform(" << entry->effectId << ") failed: " << status.getDescription();
            continue;
        }
        if (!waitFor(durationMs > 0 ? durationMs : entry->durationMs)) return;
    }
    if (callback != nullptr) callback->onComplete();
}

bool Vibrator::waitFor(int32_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mLock);
    mCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return mStop; });
    return !mStop;
}

void Vibrator::stopComposition() {
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mStop = true;
        thread = std::move(mThread);
    }
    mCv.notify_all();
    if (thread.joinable()) thread.join();
}

ndk::ScopedAStatus Vibrator::getSupportedAlwaysOnEffects(std::vector<Effect>* _aidl_return) {
    return mHal->getSupportedAlwaysOnEffects(_aidl_return);
}

ndk::ScopedAStatus Vibrator::alwaysOnEnable(int32_t id, Effect effect, EffectStrength strength) {
    return mHal->alwaysOnEnable(id, effect, strength);
}

ndk::ScopedAStatus Vibrator::alwaysOnDisable(int32_t id) {
    return mHal->alwaysOnDisable(id);
}

ndk::ScopedAStatus Vibrator::getResonantFrequency(float* _aidl_return) {
    return mHal->getResonantFrequency(_aidl_return);
}

ndk::ScopedAStatus Vibrator::getQFactor(float* _aidl_return) {
    return mHal->getQFactor(_aidl_return);
}

ndk::ScopedAStatus Vibrator::getFrequencyResolution(float* _aidl_return) {
    return mHal->getFrequencyResolution(_aidl_return);
}

ndk::ScopedAStatus Vibrator::getFrequencyMinimum(float* _aidl_return) {
    return mHal->getFrequencyMinimum(_aidl_return);
}

ndk::ScopedAStatus Vibrator::getBandwidthAmplitudeMap(std::vector<float>* _aidl_return) {
    return mHal->getBandwidthAmplitudeMap(_aidl_return);
}

ndk::ScopedAStatus Vibrator::getPwlePrimitiveDurationMax(int32_t* _aidl_return) {
    return mHal->getPwlePrimitiveDurationMax(_aidl_return);
}

ndk::ScopedAStatus Vibrator::getPwleCompositionSizeMax(int32_t* _aidl_return) {
    return mHal->getPwleCompositionSizeMax(_aidl_return);
}

ndk::ScopedAStatus Vibrator::getSupportedBraking(std::vector<Braking>* _aidl_return) {
    return mHal->getSupportedBraking(_aidl_return);
}

ndk::ScopedAStatus Vibrator::composePwle(const std::vector<PrimitivePwle>& composite,
                                         const std::shared_ptr<IVibratorCallback>& callback) {
    return mHal->composePwle(composite, callback);
}

}  // namespace aidl::android::hardware::vibrator
