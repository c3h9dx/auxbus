#pragma once
#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

class AUXBUSAnalyzerSettings : public AnalyzerSettings {
public:
    AUXBUSAnalyzerSettings();
    virtual ~AUXBUSAnalyzerSettings() {}

    virtual bool SetSettingsFromInterfaces() override;
    virtual void LoadSettings( const char* settings ) override;
    virtual const char* SaveSettings() override;

    Channel mChannel;
    // LOW przed zboczem w gore >= tego progu -> ramka wzmacniacza (K4FP),
    // krotszy -> ramka radia K* (czasy K4). Radio: 1896 us, wzmacniacz: 79 910 us.
    U32 mAmpLowThresholdUs;

protected:
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceInteger> mThresholdInterface;
};
