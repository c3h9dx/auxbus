#pragma once
#include <AnalyzerResults.h>
#include <AnalyzerHelpers.h>

// Typy ramek LLA (FrameV2 o tych samych nazwach czyta HLA)
#define FRAME_TYPE_BIT       0   // jedna probka odbiornika: mData1 = 0/1,
                                  //   mData2 = (indeks << 8) | kierunek
#define FRAME_TYPE_IDLE      1   // od konca probkowania do nastepnej ramki
#define FRAME_TYPE_PREAMBLE  2   // od zbocza w dol do okna 1. probki:
                                  //   mData1 = LOW w us, mData2 = kierunek

// Kierunek ramki (rozpoznany po dlugosci LOW)
#define DIR_K4   0   // "K*": radio (K4, pewnie tez K3) -> wzmacniacz, 24 probki w srodkach bitow K4 (476,1 us)
#define DIR_AMP  1   // "KPA*500": wzmacniacz -> radio, probkowane jak K4FP (7 x 1000 us)

class AUXBUSAnalyzerResults : public AnalyzerResults {
public:
    AUXBUSAnalyzerResults( class AUXBUSAnalyzer* analyzer, class AUXBUSAnalyzerSettings* settings );
    virtual ~AUXBUSAnalyzerResults() {}

    virtual void GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base ) override;
    virtual void GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id ) override;
    virtual void GenerateFrameTabularText( U64 frame_index, DisplayBase display_base ) override;
    virtual void GeneratePacketTabularText( U64 packet_id, DisplayBase display_base ) override;
    virtual void GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base ) override;

protected:
    AUXBUSAnalyzer*         mAnalyzer;
    AUXBUSAnalyzerSettings* mSettings;
};
