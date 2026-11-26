/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vibratorfeature-wrapper"

#include "Vibrator.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android/binder_parcel.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace aidl::android::hardware::vibrator {

using VibratorConfig = Vibrator::VibratorConfig;

namespace {

struct PrimitiveEffect {
    CompositePrimitive primitive;
    int32_t effectId;
    int32_t durationMs;
};

constexpr PrimitiveEffect kPrimitiveEffects[] = {
        {CompositePrimitive::NOOP, -1, 0},
        {CompositePrimitive::CLICK, 88, 15},
        {CompositePrimitive::THUD, 163, 23},
        {CompositePrimitive::SPIN, 170, 196},
        {CompositePrimitive::QUICK_RISE, 162, 31},
        {CompositePrimitive::SLOW_RISE, 57, 215},
        {CompositePrimitive::QUICK_FALL, 188, 25},
        {CompositePrimitive::LIGHT_TICK, 86, 10},
        {CompositePrimitive::LOW_TICK, 86, 10},
};

constexpr int32_t kCompositionDelayMaxMs = 1000;
constexpr int32_t kCompositionSizeMax = 32;

constexpr std::array<int32_t, 33> kAllowedEffectIds = {
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 57, 84, 86, 88, 91, 92, 102, 162, 163, 165, 166,
        167, 168, 169, 170, 171, 172, 181, 183, 185, 186,
};

constexpr std::array<int32_t, 4> kExtraAllowedIds = {187, 188, 190, 203};

bool isAllowedEffectId(int32_t effectId) {
    return std::find(kAllowedEffectIds.begin(), kAllowedEffectIds.end(), effectId) !=
                   kAllowedEffectIds.end() ||
           std::find(kExtraAllowedIds.begin(), kExtraAllowedIds.end(), effectId) !=
                   kExtraAllowedIds.end();
}

int32_t sanitizeEffectId(int32_t effectId, int32_t fallback) {
    return isAllowedEffectId(effectId) ? effectId : fallback;
}

int32_t clampStrengthPct(int32_t pct) {
    if (pct <= VibratorConfig::STRENGTH_OFF) return VibratorConfig::STRENGTH_OFF;
    return std::clamp<int32_t>(pct, VibratorConfig::STRENGTH_LIGHT, VibratorConfig::STRENGTH_STRONG);
}

int32_t effectStrengthToPct(EffectStrength strength) {
    switch (strength) {
        case EffectStrength::LIGHT:
            return VibratorConfig::STRENGTH_LIGHT;
        case EffectStrength::MEDIUM:
            return VibratorConfig::STRENGTH_MEDIUM;
        case EffectStrength::STRONG:
        default:
            return VibratorConfig::STRENGTH_STRONG;
    }
}

EffectStrength pctToEffectStrength(int32_t pct) {
    if (pct <= VibratorConfig::STRENGTH_LIGHT) return EffectStrength::LIGHT;
    if (pct <= VibratorConfig::STRENGTH_MEDIUM) return EffectStrength::MEDIUM;
    return EffectStrength::STRONG;
}

// Returns false when the category ceiling is explicitly disabled (STRENGTH_OFF = 0%).
bool capGainPct(int32_t requestedPct, int32_t maxPct, int32_t* outPct) {
    const int32_t clampedMax = clampStrengthPct(maxPct);
    if (clampedMax <= VibratorConfig::STRENGTH_OFF) return false;
    *outPct = std::clamp<int32_t>(std::min(requestedPct, clampedMax),
                                  VibratorConfig::STRENGTH_LIGHT,
                                  VibratorConfig::STRENGTH_STRONG);
    return true;
}

VibratorConfig makeBalancedConfig() {
    VibratorConfig cfg;
    cfg.profile = VibratorConfig::PROFILE_BALANCED;
    cfg.stockSliderSafety = true;
    cfg.scaleBias = 0;
    cfg.clickEffectId = 88;
    cfg.clickMaxStrength = VibratorConfig::STRENGTH_MEDIUM;
    cfg.tickEffectId = 86;
    cfg.tickMaxStrength = VibratorConfig::STRENGTH_MEDIUM;
    cfg.heavyEffectId = 163;
    cfg.heavyMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.sliderEffectId = 92;
    cfg.sliderStrength = VibratorConfig::STRENGTH_LIGHT;
    cfg.quickRiseEffectId = 162;
    cfg.quickRiseMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.quickFallEffectId = 188;
    cfg.quickFallMaxStrength = VibratorConfig::STRENGTH_MEDIUM;
    cfg.slowRiseEffectId = 57;
    cfg.slowRiseMaxStrength = VibratorConfig::STRENGTH_MEDIUM;
    cfg.spinEffectId = 170;
    cfg.spinMaxStrength = VibratorConfig::STRENGTH_MEDIUM;
    return cfg;
}

VibratorConfig makeCrispTightConfig() {
    VibratorConfig cfg = makeBalancedConfig();
    cfg.profile = VibratorConfig::PROFILE_CRISP_TIGHT;
    cfg.clickEffectId = 6;
    cfg.clickMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.tickEffectId = 86;
    cfg.tickMaxStrength = VibratorConfig::STRENGTH_LIGHT;
    cfg.heavyEffectId = 88;
    cfg.heavyMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.sliderEffectId = 102;
    cfg.sliderStrength = VibratorConfig::STRENGTH_LIGHT;
    cfg.quickRiseEffectId = 188;
    return cfg;
}

VibratorConfig makeThinLightConfig() {
    VibratorConfig cfg = makeBalancedConfig();
    cfg.profile = VibratorConfig::PROFILE_THIN_LIGHT;
    cfg.clickEffectId = 92;
    cfg.clickMaxStrength = VibratorConfig::STRENGTH_MEDIUM;
    cfg.tickEffectId = 92;
    cfg.tickMaxStrength = VibratorConfig::STRENGTH_LIGHT;
    cfg.heavyEffectId = 188;
    cfg.heavyMaxStrength = VibratorConfig::STRENGTH_MEDIUM;
    cfg.sliderEffectId = 92;
    cfg.sliderStrength = VibratorConfig::STRENGTH_LIGHT;
    cfg.quickRiseEffectId = 188;
    cfg.quickRiseMaxStrength = VibratorConfig::STRENGTH_LIGHT;
    return cfg;
}

VibratorConfig makeStockConfig(bool sliderSafety) {
    VibratorConfig cfg;
    cfg.profile = VibratorConfig::PROFILE_STOCK;
    cfg.stockSliderSafety = sliderSafety;
    cfg.scaleBias = 0;
    cfg.clickEffectId = 0;
    cfg.clickMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.tickEffectId = 7;
    cfg.tickMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.heavyEffectId = 5;
    cfg.heavyMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.sliderEffectId = 8;
    cfg.sliderStrength =
            sliderSafety ? VibratorConfig::STRENGTH_LIGHT : VibratorConfig::STRENGTH_STRONG;
    cfg.quickRiseEffectId = 4;
    cfg.quickRiseMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.quickFallEffectId = 6;
    cfg.quickFallMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.slowRiseEffectId = 5;
    cfg.slowRiseMaxStrength = VibratorConfig::STRENGTH_STRONG;
    cfg.spinEffectId = 3;
    cfg.spinMaxStrength = VibratorConfig::STRENGTH_STRONG;
    return cfg;
}

VibratorConfig sanitizeConfig(const VibratorConfig& in) {
    const VibratorConfig defaults = makeBalancedConfig();
    VibratorConfig out = in;
    const bool validProfile =
            in.profile == VibratorConfig::PROFILE_BALANCED ||
            in.profile == VibratorConfig::PROFILE_STOCK ||
            in.profile == VibratorConfig::PROFILE_CRISP_TIGHT ||
            in.profile == VibratorConfig::PROFILE_THIN_LIGHT ||
            in.profile == VibratorConfig::PROFILE_CUSTOM;
    out.profile = validProfile ? in.profile : VibratorConfig::PROFILE_STOCK;
    out.scaleBias = std::clamp(in.scaleBias, -2, 2);
    out.clickEffectId = sanitizeEffectId(in.clickEffectId, defaults.clickEffectId);
    out.clickMaxStrength = clampStrengthPct(in.clickMaxStrength);
    out.tickEffectId = sanitizeEffectId(in.tickEffectId, defaults.tickEffectId);
    out.tickMaxStrength = clampStrengthPct(in.tickMaxStrength);
    out.heavyEffectId = sanitizeEffectId(in.heavyEffectId, defaults.heavyEffectId);
    out.heavyMaxStrength = clampStrengthPct(in.heavyMaxStrength);
    out.sliderEffectId = sanitizeEffectId(in.sliderEffectId, defaults.sliderEffectId);
    out.sliderStrength = clampStrengthPct(in.sliderStrength);
    out.quickRiseEffectId = sanitizeEffectId(in.quickRiseEffectId, defaults.quickRiseEffectId);
    out.quickRiseMaxStrength = clampStrengthPct(in.quickRiseMaxStrength);
    out.quickFallEffectId = sanitizeEffectId(in.quickFallEffectId, defaults.quickFallEffectId);
    out.quickFallMaxStrength = clampStrengthPct(in.quickFallMaxStrength);
    out.slowRiseEffectId = sanitizeEffectId(in.slowRiseEffectId, defaults.slowRiseEffectId);
    out.slowRiseMaxStrength = clampStrengthPct(in.slowRiseMaxStrength);
    out.spinEffectId = sanitizeEffectId(in.spinEffectId, defaults.spinEffectId);
    out.spinMaxStrength = clampStrengthPct(in.spinMaxStrength);
    return out;
}

VibratorConfig resolveEffectiveConfig(const VibratorConfig& cfg) {
    switch (cfg.profile) {
        case VibratorConfig::PROFILE_BALANCED:
            return makeBalancedConfig();
        case VibratorConfig::PROFILE_CRISP_TIGHT:
            return makeCrispTightConfig();
        case VibratorConfig::PROFILE_THIN_LIGHT:
            return makeThinLightConfig();
        case VibratorConfig::PROFILE_CUSTOM:
            return cfg;
        case VibratorConfig::PROFILE_STOCK:
        default:
            return makeStockConfig(cfg.stockSliderSafety);
    }
}

const PrimitiveEffect* findPrimitive(CompositePrimitive primitive) {
    for (const auto& entry : kPrimitiveEffects) {
        if (entry.primitive == primitive) return &entry;
    }
    return nullptr;
}

bool mapPrebakedEffect(const VibratorConfig& cfg, Effect effect, EffectStrength strength,
                       Effect* outEffect, int32_t* outGainPct) {
    *outEffect = effect;
    *outGainPct = effectStrengthToPct(strength);

    if (cfg.profile == VibratorConfig::PROFILE_STOCK) {
        return true;
    }

    int32_t mappedId = static_cast<int32_t>(effect);
    int32_t requestedPct = effectStrengthToPct(strength);
    int32_t cappedPct = requestedPct;
    bool enabled = true;

    switch (effect) {
        case Effect::CLICK:
        case Effect::POP:
            mappedId = cfg.clickEffectId;
            enabled = capGainPct(requestedPct, cfg.clickMaxStrength, &cappedPct);
            break;
        case Effect::DOUBLE_CLICK:
        case Effect::THUD:
        case Effect::HEAVY_CLICK:
            mappedId = cfg.heavyEffectId;
            enabled = capGainPct(requestedPct, cfg.heavyMaxStrength, &cappedPct);
            break;
        case Effect::TICK:
            mappedId = cfg.tickEffectId;
            if (strength == EffectStrength::MEDIUM) {
                requestedPct = VibratorConfig::STRENGTH_LIGHT;
            } else if (strength == EffectStrength::STRONG) {
                requestedPct = VibratorConfig::STRENGTH_MEDIUM;
            }
            enabled = capGainPct(requestedPct, cfg.tickMaxStrength, &cappedPct);
            break;
        case Effect::TEXTURE_TICK:
            mappedId = cfg.sliderEffectId;
            enabled = capGainPct(requestedPct, cfg.sliderStrength, &cappedPct);
            break;
        default:
            break;
    }

    if (!enabled || mappedId < 0) return false;
    *outEffect = static_cast<Effect>(mappedId);
    *outGainPct = cappedPct;
    return true;
}

int32_t scaleToGainPct(float scale) {
    if (scale <= 0.45f) return VibratorConfig::STRENGTH_LIGHT;
    if (scale <= 0.85f) return VibratorConfig::STRENGTH_MEDIUM;
    return VibratorConfig::STRENGTH_STRONG;
}

bool resolvePrimitivePlay(const VibratorConfig& cfg, const PrimitiveEffect* entry, float scale,
                          int32_t* outEffectId, int32_t* outGainPct) {
    if (entry->primitive == CompositePrimitive::NOOP || scale <= 0.0f) return false;

    const float adjusted =
            std::clamp(scale + static_cast<float>(cfg.scaleBias) * 0.10f, 0.01f, 1.0f);

    if (cfg.profile == VibratorConfig::PROFILE_STOCK) {
        *outEffectId = static_cast<int32_t>(entry->primitive);
        if (cfg.stockSliderSafety &&
            (entry->primitive == CompositePrimitive::LIGHT_TICK ||
             entry->primitive == CompositePrimitive::LOW_TICK) &&
            scale <= 0.35f) {
            *outGainPct = VibratorConfig::STRENGTH_LIGHT;
        } else {
            *outGainPct = scaleToGainPct(adjusted);
        }
        return true;
    }

    int32_t effectId = entry->effectId;
    int32_t maxGainPct = VibratorConfig::STRENGTH_STRONG;
    int32_t rawGainPct = scaleToGainPct(adjusted);

    switch (entry->primitive) {
        case CompositePrimitive::LIGHT_TICK:
        case CompositePrimitive::LOW_TICK:
            if (scale <= 0.35f) {
                effectId = cfg.sliderEffectId;
                maxGainPct = cfg.sliderStrength;
                rawGainPct = VibratorConfig::STRENGTH_STRONG;
            } else {
                effectId = cfg.tickEffectId;
                maxGainPct = cfg.tickMaxStrength;
                rawGainPct = adjusted < 0.85f
                                     ? VibratorConfig::STRENGTH_LIGHT
                                     : (adjusted < 0.95f ? VibratorConfig::STRENGTH_MEDIUM
                                                         : VibratorConfig::STRENGTH_STRONG);
            }
            break;
        case CompositePrimitive::CLICK:
            effectId = cfg.clickEffectId;
            maxGainPct = cfg.clickMaxStrength;
            rawGainPct = adjusted <= 0.45f
                                 ? VibratorConfig::STRENGTH_LIGHT
                                 : (adjusted <= 0.90f ? VibratorConfig::STRENGTH_MEDIUM
                                                      : VibratorConfig::STRENGTH_STRONG);
            break;
        case CompositePrimitive::THUD:
            effectId = cfg.heavyEffectId;
            maxGainPct = cfg.heavyMaxStrength;
            break;
        case CompositePrimitive::QUICK_RISE:
            effectId = cfg.quickRiseEffectId;
            maxGainPct = cfg.quickRiseMaxStrength;
            break;
        case CompositePrimitive::QUICK_FALL:
            effectId = cfg.quickFallEffectId;
            maxGainPct = cfg.quickFallMaxStrength;
            break;
        case CompositePrimitive::SLOW_RISE:
            effectId = cfg.slowRiseEffectId;
            maxGainPct = cfg.slowRiseMaxStrength;
            break;
        case CompositePrimitive::SPIN:
            effectId = cfg.spinEffectId;
            maxGainPct = cfg.spinMaxStrength;
            break;
        default:
            break;
    }

    if (effectId < 0) return false;
    *outEffectId = effectId;
    return capGainPct(rawGainPct, maxGainPct, outGainPct);
}

void* dummyExtCreate(void*) { return nullptr; }
void dummyExtDestroy(void*) {}
binder_status_t dummyExtTransact(AIBinder*, transaction_code_t, const AParcel*, AParcel*) {
    return STATUS_UNKNOWN_TRANSACTION;
}

ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace

Vibrator::Vibrator(std::shared_ptr<IVibrator> hal)
    : mHal(std::move(hal)), mConfig(makeStockConfig(true)) {
    if (mHal != nullptr) {
        AIBinder* rawExt = nullptr;
        if (AIBinder_getExtension(mHal->asBinder().get(), &rawExt) == STATUS_OK &&
            rawExt != nullptr) {
            AIBinder_Class* extClass = AIBinder_Class_define(
                    "vendor.hardware.vibratorfeature.IVibratorExt", dummyExtCreate,
                    dummyExtDestroy, dummyExtTransact);
            if (extClass != nullptr && AIBinder_associateClass(rawExt, extClass)) {
                mExtBinder.set(rawExt);
                LOG(INFO) << "Attached IVibratorExt extension for continuous gain scaling";
            } else {
                AIBinder_decStrong(rawExt);
            }
        }
    }
}

void Vibrator::sendAmplitudeExtLocked(float amplitude, int32_t mode) {
    if (mExtBinder.get() == nullptr) return;
    AParcel* in = nullptr;
    AParcel* out = nullptr;
    if (AIBinder_prepareTransaction(mExtBinder.get(), &in) != STATUS_OK || in == nullptr) {
        return;
    }
    AParcel_writeFloat(in, amplitude);
    AParcel_writeInt32(in, mode);
    if (AIBinder_transact(mExtBinder.get(), FIRST_CALL_TRANSACTION + 4, &in, &out, 0) ==
        STATUS_OK) {
        AStatus* st = nullptr;
        if (out != nullptr && AParcel_readStatusHeader(out, &st) == STATUS_OK && st != nullptr) {
            AStatus_delete(st);
        }
    }
    if (out != nullptr) AParcel_delete(out);
}

ndk::ScopedAStatus Vibrator::performWithGainLocked(
        Effect effect, int32_t gainPct, const std::shared_ptr<IVibratorCallback>& callback,
        int32_t* durationMs) {
    const int32_t clamped = std::clamp(gainPct, 30, 100);
    // Exact preset steps (30/50/100) map directly to LIGHT/MEDIUM/STRONG with zero extra IPC.
    if (clamped == 30 || clamped == 50 || clamped == 100 || mExtBinder.get() == nullptr) {
        return mHal->perform(effect, pctToEffectStrength(clamped), callback, durationMs);
    }
    const float amp = static_cast<float>(clamped) / 100.0f;
    // Xiaomi's vibratorfeature HAL overrides perform() gain when both LowPower(3) and Once(1)
    // multipliers are set; restore LowPower(3)=1.0f immediately afterwards so stock calls stay intact.
    sendAmplitudeExtLocked(amp, 3);
    sendAmplitudeExtLocked(amp, 1);
    auto status = mHal->perform(effect, EffectStrength::STRONG, callback, durationMs);
    sendAmplitudeExtLocked(1.0f, 3);
    return status;
}

Vibrator::~Vibrator() {
    stopComposition();
}

VibratorConfig Vibrator::getConfig() const {
    std::lock_guard<std::mutex> lock(mConfigLock);
    return mConfig;
}

void Vibrator::setConfig(const VibratorConfig& config) {
    std::lock_guard<std::mutex> lock(mConfigLock);
    mConfig = sanitizeConfig(config);
    LOG(INFO) << "Updated haptic config: profile=" << mConfig.profile
              << " click=" << mConfig.clickEffectId << "/" << mConfig.clickMaxStrength
              << " tick=" << mConfig.tickEffectId << "/" << mConfig.tickMaxStrength
              << " heavy=" << mConfig.heavyEffectId << "/" << mConfig.heavyMaxStrength
              << " slider=" << mConfig.sliderEffectId << "/" << mConfig.sliderStrength
              << " rise=" << mConfig.quickRiseEffectId << "/" << mConfig.quickRiseMaxStrength
              << " fall=" << mConfig.quickFallEffectId << "/" << mConfig.quickFallMaxStrength
              << " slow=" << mConfig.slowRiseEffectId << "/" << mConfig.slowRiseMaxStrength
              << " spin=" << mConfig.spinEffectId << "/" << mConfig.spinMaxStrength
              << " bias=" << mConfig.scaleBias
              << " stockSafety=" << mConfig.stockSliderSafety;
}

void Vibrator::resetConfig() {
    setConfig(makeStockConfig(true));
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
    const VibratorConfig cfg = resolveEffectiveConfig(getConfig());
    Effect mappedEffect;
    int32_t mappedGainPct = 100;
    if (!mapPrebakedEffect(cfg, effect, strength, &mappedEffect, &mappedGainPct)) {
        *_aidl_return = 0;
        if (callback != nullptr) callback->onComplete();
        return ndk::ScopedAStatus::ok();
    }
    std::lock_guard<std::mutex> halLock(mHalLock);
    if (cfg.profile == VibratorConfig::PROFILE_STOCK) {
        return mHal->perform(mappedEffect, strength, callback, _aidl_return);
    }
    return performWithGainLocked(mappedEffect, mappedGainPct, callback, _aidl_return);
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

    const VibratorConfig cfg = resolveEffectiveConfig(getConfig());
    stopComposition();
    std::lock_guard<std::mutex> lock(mLock);
    mStop = false;
    mThread = std::thread(&Vibrator::playComposition, this, composite, cfg, callback);
    return ndk::ScopedAStatus::ok();
}

void Vibrator::playComposition(std::vector<CompositeEffect> composite,
                               VibratorConfig config,
                               std::shared_ptr<IVibratorCallback> callback) {
    int32_t offOverheadMs = 0;

    for (size_t i = 0; i < composite.size(); ++i) {
        const auto& e = composite[i];
        int32_t delayMs = std::max(0, e.delayMs - offOverheadMs);
        offOverheadMs = 0;

        if (delayMs > 0 && !waitFor(delayMs)) return;

        const PrimitiveEffect* entry = findPrimitive(e.primitive);
        int32_t effectId = -1;
        int32_t gainPct = 30;
        if (!resolvePrimitivePlay(config, entry, e.scale, &effectId, &gainPct)) continue;

        int32_t durationMs = 0;
        ndk::ScopedAStatus status = ndk::ScopedAStatus::ok();
        {
            std::lock_guard<std::mutex> halLock(mHalLock);
            status = performWithGainLocked(static_cast<Effect>(effectId), gainPct, nullptr,
                                           &durationMs);
        }
        if (!status.isOk()) {
            LOG(WARNING) << "perform(" << effectId << ") failed: " << status.getDescription();
            continue;
        }

        if (!waitFor(durationMs > 0 ? durationMs : entry->durationMs)) {
            mHal->off();
            return;
        }

        // VibratorCL rejects subsequent perform() calls with EX_UNSUPPORTED_OPERATION while
        // ActiveUsecase is still set; call off() between primitives and account for its
        // minimum-interval guard sleep against the next primitive's delay.
        if (i + 1 < composite.size()) {
            auto offStart = std::chrono::steady_clock::now();
            mHal->off();
            offOverheadMs = static_cast<int32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - offStart)
                            .count());
        }
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

VibratorTuner::VibratorTuner(std::shared_ptr<Vibrator> vibrator)
    : mVibrator(std::move(vibrator)) {}

ndk::ScopedAStatus VibratorTuner::getConfig(Vibrator::VibratorConfig* _aidl_return) {
    *_aidl_return = mVibrator->getConfig();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorTuner::setConfig(const Vibrator::VibratorConfig& config) {
    mVibrator->setConfig(config);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorTuner::resetToDefault() {
    mVibrator->resetConfig();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorTuner::getResonantFrequency(float* _aidl_return) {
    float f0 = 0.0f;
    if (mVibrator->getResonantFrequency(&f0).isOk() && std::isfinite(f0) && f0 > 0.0f) {
        *_aidl_return = f0;
        return ndk::ScopedAStatus::ok();
    }
    std::string raw;
    if (::android::base::ReadFileToString("/mnt/vendor/persist/haptics/vib_cal_f0", &raw)) {
        int32_t tenthHz = 0;
        if (::android::base::ParseInt(::android::base::Trim(raw), &tenthHz) && tenthHz > 0) {
            *_aidl_return = static_cast<float>(tenthHz) / 10.0f;
            return ndk::ScopedAStatus::ok();
        }
    }
    *_aidl_return = 0.0f;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorTuner::getQFactor(float* _aidl_return) {
    float q = 0.0f;
    if (mVibrator->getQFactor(&q).isOk() && std::isfinite(q) && q > 0.0f) {
        *_aidl_return = q;
    } else {
        *_aidl_return = 0.0f;
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::vibrator
