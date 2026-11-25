#include "AUXBUSAnalyzerResults.h"
#include "AUXBUSAnalyzer.h"
#include "AUXBUSAnalyzerSettings.h"
#include <AnalyzerHelpers.h>
#include <cstdio>

AUXBUSAnalyzerResults::AUXBUSAnalyzerResults( AUXBUSAnalyzer* analyzer, AUXBUSAnalyzerSettings* settings ):
    mAnalyzer( analyzer ),
    mSettings( settings ) {
}

static const char* dir_name( U64 d ) {
    return ( d & 0xFF ) == DIR_AMP ? "KPA*500" : "K*";
}

void AUXBUSAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base ) {
    Frame frame = GetFrame( frame_index );
    ClearResultStrings();
    char buf[64];

    if( frame.mType == FRAME_TYPE_IDLE ) {
        AddResultString( "I" );
        AddResultString( "IDLE" );
    }
    else if( frame.mType == FRAME_TYPE_PREAMBLE ){
        const char* d = dir_name( frame.mData2 );
        AddResultString( d );
        snprintf( buf, sizeof( buf ), "%s PRE", d );
        AddResultString( buf );
        snprintf( buf, sizeof( buf ), "%s PRE LOW %llu us%s", d, (unsigned long long)frame.mData1,
                  ( frame.mFlags & DISPLAY_AS_ERROR_FLAG ) ? " (collision)" :
                  ( ( frame.mData2 & 0xFF ) == DIR_AMP && ( frame.mFlags & DISPLAY_AS_WARNING_FLAG ) ) ? " (response tail)" : "" );
        AddResultString( buf );
    }
    else {
        snprintf( buf, sizeof( buf ), "%d", (int)frame.mData1 );
        AddResultString( buf );
        snprintf( buf, sizeof( buf ), "%d:%d", (int)( frame.mData2 >> 8 ), (int)frame.mData1 );
        AddResultString( buf );
    }
}

void AUXBUSAnalyzerResults::GenerateExportFile( const char*, DisplayBase, U32 ) {
}

void AUXBUSAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base ) {
    Frame frame = GetFrame( frame_index );
    ClearResultStrings();
    char buf[64];
    if( frame.mType == FRAME_TYPE_IDLE )
        AddResultString( "IDLE" );
    else if( frame.mType == FRAME_TYPE_PREAMBLE ) {
        snprintf( buf, sizeof( buf ), "%s PRE LOW %llu us%s", dir_name( frame.mData2 ),
                  (unsigned long long)frame.mData1,
                  ( frame.mFlags & DISPLAY_AS_ERROR_FLAG ) ? " (collision)" :
                  ( ( frame.mData2 & 0xFF ) == DIR_AMP && ( frame.mFlags & DISPLAY_AS_WARNING_FLAG ) ) ? " (response tail)" : "" );
        AddResultString( buf );
    }
    else {
        snprintf( buf, sizeof( buf ), "%s bit %d = %d", dir_name( frame.mData2 ),
                  (int)( frame.mData2 >> 8 ), (int)frame.mData1 );
        AddResultString( buf );
    }
}

void AUXBUSAnalyzerResults::GeneratePacketTabularText( U64 packet_id, DisplayBase display_base ) {
    ClearResultStrings();
    AddResultString( "" );
}

void AUXBUSAnalyzerResults::GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base ) {
    ClearResultStrings();
    AddResultString( "" );
}
