#include "AUXBUSAnalyzerSettings.h"
#include <AnalyzerHelpers.h>

static const U32 DEFAULT_AMP_LOW_US = 10000;

AUXBUSAnalyzerSettings::AUXBUSAnalyzerSettings():
    mChannel( UNDEFINED_CHANNEL ),
    mAmpLowThresholdUs( DEFAULT_AMP_LOW_US ) {

    mChannelInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mChannelInterface->SetTitleAndTooltip( "AUXBUS",
        "Elecraft AUXBUS line (idle HIGH, active LOW)" );
    mChannelInterface->SetChannel( mChannel );

    mThresholdInterface.reset( new AnalyzerSettingInterfaceInteger() );
    mThresholdInterface->SetTitleAndTooltip( "Amplifier LOW threshold [us]",
        "LOW before the rising edge >= threshold: amplifier frame KPA*500 (sampled like K4FP, 7 x 1000 us). "
        "Shorter: radio frame K* (24 samples at K4 bit centres, 476.1 us). Radio 1896 us, amplifier 79910 us." );
    mThresholdInterface->SetMin( 2500 );
    mThresholdInterface->SetMax( 79000 );
    mThresholdInterface->SetInteger( mAmpLowThresholdUs );

    AddInterface( mChannelInterface.get() );
    AddInterface( mThresholdInterface.get() );

    ClearChannels();
    AddChannel( mChannel, "AUXBUS", false );
}

bool AUXBUSAnalyzerSettings::SetSettingsFromInterfaces() {
    mChannel           = mChannelInterface->GetChannel();
    mAmpLowThresholdUs = (U32)mThresholdInterface->GetInteger();
    ClearChannels();
    AddChannel( mChannel, "AUXBUS", true );
    return true;
}

void AUXBUSAnalyzerSettings::LoadSettings( const char* settings ) {
    SimpleArchive text_archive;
    text_archive.SetString( settings );
    text_archive >> mChannel;
    U32 thr;
    // starsze zapisane ustawienia nie maja progu
    mAmpLowThresholdUs = ( text_archive >> thr ) ? thr : DEFAULT_AMP_LOW_US;
    ClearChannels();
    AddChannel( mChannel, "AUXBUS", true );
    mChannelInterface->SetChannel( mChannel );
    mThresholdInterface->SetInteger( mAmpLowThresholdUs );
}

const char* AUXBUSAnalyzerSettings::SaveSettings() {
    SimpleArchive text_archive;
    text_archive << mChannel;
    text_archive << mAmpLowThresholdUs;
    return SetReturnString( text_archive.GetString() );
}
