/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vibratorfeature-wrapper"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <unistd.h>

#include "Vibrator.h"

using aidl::android::hardware::vibrator::IVibrator;
using aidl::android::hardware::vibrator::Vibrator;
using aidl::android::hardware::vibrator::VibratorTuner;

namespace {

// The stock HAL is restarted by init; restart with it to hold a binder to the new instance
void onHalDied(void*) {
    LOG(ERROR) << "Stock vibrator HAL died, restarting";
    _exit(EXIT_FAILURE);
}

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(2);

    const std::string halInstance = std::string(IVibrator::descriptor) + "/xiaomi";
    ndk::SpAIBinder halBinder(AServiceManager_waitForService(halInstance.c_str()));
    std::shared_ptr<IVibrator> hal = IVibrator::fromBinder(halBinder);
    CHECK(hal != nullptr) << "No " << halInstance;

    static ndk::ScopedAIBinder_DeathRecipient deathRecipient(
            AIBinder_DeathRecipient_new(onHalDied));
    if (AIBinder_linkToDeath(halBinder.get(), deathRecipient.get(), nullptr) != STATUS_OK) {
        LOG(WARNING) << "Failed to link to the death of " << halInstance;
    }

    auto vibrator = ndk::SharedRefBase::make<Vibrator>(hal);
    const std::string instance = std::string(IVibrator::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(vibrator->asBinder().get(), instance.c_str()), STATUS_OK);

    auto tuner = ndk::SharedRefBase::make<VibratorTuner>(vibrator);
    const std::string tunerInstance = std::string(VibratorTuner::descriptor) + "/default";
    if (AServiceManager_addService(tuner->asBinder().get(), tunerInstance.c_str()) != STATUS_OK) {
        LOG(WARNING) << "Failed to register " << tunerInstance;
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not be reached
}
