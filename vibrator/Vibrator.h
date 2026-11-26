/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <aidl/vendor/xiaomi/hardware/vibratortuner/BnVibratorTuner.h>
#include <android/binder_ibinder.h>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace aidl::android::hardware::vibrator {

// IVibrator/default on top of the stock Xiaomi HAL (IVibrator/xiaomi). Everything is forwarded,
// except prebaked/composition mappings and runtime calibration configured via IVibratorTuner.
class Vibrator : public BnVibrator {
  public:
    using VibratorConfig = ::aidl::vendor::xiaomi::hardware::vibratortuner::VibratorConfig;

    explicit Vibrator(std::shared_ptr<IVibrator> hal);
    ~Vibrator() override;

    VibratorConfig getConfig() const;
    void setConfig(const VibratorConfig& config);
    void resetConfig();

    ndk::ScopedAStatus getCapabilities(int32_t* _aidl_return) override;
    ndk::ScopedAStatus off() override;
    ndk::ScopedAStatus on(int32_t timeoutMs,
                          const std::shared_ptr<IVibratorCallback>& callback) override;
    ndk::ScopedAStatus perform(Effect effect, EffectStrength strength,
                               const std::shared_ptr<IVibratorCallback>& callback,
                               int32_t* _aidl_return) override;
    ndk::ScopedAStatus getSupportedEffects(std::vector<Effect>* _aidl_return) override;
    ndk::ScopedAStatus setAmplitude(float amplitude) override;
    ndk::ScopedAStatus setExternalControl(bool enabled) override;
    ndk::ScopedAStatus getCompositionDelayMax(int32_t* _aidl_return) override;
    ndk::ScopedAStatus getCompositionSizeMax(int32_t* _aidl_return) override;
    ndk::ScopedAStatus getSupportedPrimitives(std::vector<CompositePrimitive>* _aidl_return) override;
    ndk::ScopedAStatus getPrimitiveDuration(CompositePrimitive primitive,
                                            int32_t* _aidl_return) override;
    ndk::ScopedAStatus compose(const std::vector<CompositeEffect>& composite,
                               const std::shared_ptr<IVibratorCallback>& callback) override;
    ndk::ScopedAStatus getSupportedAlwaysOnEffects(std::vector<Effect>* _aidl_return) override;
    ndk::ScopedAStatus alwaysOnEnable(int32_t id, Effect effect, EffectStrength strength) override;
    ndk::ScopedAStatus alwaysOnDisable(int32_t id) override;
    ndk::ScopedAStatus getResonantFrequency(float* _aidl_return) override;
    ndk::ScopedAStatus getQFactor(float* _aidl_return) override;
    ndk::ScopedAStatus getFrequencyResolution(float* _aidl_return) override;
    ndk::ScopedAStatus getFrequencyMinimum(float* _aidl_return) override;
    ndk::ScopedAStatus getBandwidthAmplitudeMap(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus getPwlePrimitiveDurationMax(int32_t* _aidl_return) override;
    ndk::ScopedAStatus getPwleCompositionSizeMax(int32_t* _aidl_return) override;
    ndk::ScopedAStatus getSupportedBraking(std::vector<Braking>* _aidl_return) override;
    ndk::ScopedAStatus composePwle(const std::vector<PrimitivePwle>& composite,
                                   const std::shared_ptr<IVibratorCallback>& callback) override;

  private:
    void playComposition(std::vector<CompositeEffect> composite,
                         VibratorConfig config,
                         std::shared_ptr<IVibratorCallback> callback);
    void stopComposition();
    bool waitFor(int32_t timeoutMs);
    ndk::ScopedAStatus performWithGainLocked(Effect effect, int32_t gainPct,
                                             const std::shared_ptr<IVibratorCallback>& callback,
                                             int32_t* durationMs);
    void sendAmplitudeExtLocked(float amplitude, int32_t mode);

    std::shared_ptr<IVibrator> mHal;
    ndk::SpAIBinder mExtBinder;
    std::thread mThread;
    std::mutex mLock;
    std::mutex mHalLock;
    mutable std::mutex mConfigLock;
    VibratorConfig mConfig;
    std::condition_variable mCv;
    bool mStop = false;
};

// Companion vendor AIDL service allowing XiaomiParts to inspect calibration telemetry and
// switch haptic profiles or per-category waveforms/strengths at runtime.
class VibratorTuner : public ::aidl::vendor::xiaomi::hardware::vibratortuner::BnVibratorTuner {
  public:
    explicit VibratorTuner(std::shared_ptr<Vibrator> vibrator);

    ndk::ScopedAStatus getConfig(Vibrator::VibratorConfig* _aidl_return) override;
    ndk::ScopedAStatus setConfig(const Vibrator::VibratorConfig& config) override;
    ndk::ScopedAStatus resetToDefault() override;
    ndk::ScopedAStatus getResonantFrequency(float* _aidl_return) override;
    ndk::ScopedAStatus getQFactor(float* _aidl_return) override;

  private:
    std::shared_ptr<Vibrator> mVibrator;
};

}  // namespace aidl::android::hardware::vibrator
