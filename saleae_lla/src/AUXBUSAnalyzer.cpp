#include "AUXBUSAnalyzer.h"
#include "AUXBUSAnalyzerSettings.h"
#include "AUXBUSAnalyzerResults.h"
#include <AnalyzerChannelData.h>

// Elecraft AUXBUS (K4 <-> KPA500/KPA1500), spoczynek HIGH, aktywny LOW.
// Opis protokolu: auxbus/doc/FRAME_GENERATOR_SPEC.md.
//
// Analizator NIE mierzy szerokosci impulsow. Synchronizuje sie do zbocza w gore
// konczacego LOW preambuly (t_r) i od niego odlicza probki tak, jak robi to
// odbiornik danej czesci ramki (bez modelowania opoznien przerwan).
//
// Kierunek rozpoznaje dlugosc LOW przed t_r:
//   LOW <  prog (domyslnie 10 ms) -> ramka radia K* (preambula LOW 1896 us)
//   LOW >= prog                   -> ramka wzmacniacza KPA*500 (LOW 79,91 ms)
//
// Ramka K*: czasy z K4 (zmierzony bit 476,1 us; czasow odbiornika KPA1500
//   znanych tylko z analizy kodu nie uzywamy). Od t_r trwa bit '1'
//   preambuly, potem bity danych; probka w srodku bitu k:
//   t_r + 714,15 + 476,1*k us, zawsze 24 probki. Ramka 16-bitowa daje 8
//   koncowych probek na spoczynkowym HIGH.
// Ramka polecenia (pierwszy bajt poza 0x40-0x7F) - response odbiera K4FP
//   oknem nasluchu: koniec_ramki + 238,1 + 476,1*j us, gdzie koniec_ramki =
//   t_r + 17*476,1 us. To ta sama siatka co wyzej (probki 16..23).
//   K4FP konczy nasluch na ostatniej probce. Jesli od tej chwili do 4,4 ms
//   po ramce linia jest LOW (np. KRC2 odpowiada 274-290 us po ramce i konczy
//   po ~4,1 ms), K4FP uzna to za ramke wzmacniacza: LLA dekoduje ja modelem
//   K4FP i oznacza jako ogon (tail).
//
// Ramka KPA*500 -> radio, odbiera K4FP (Timer5 + Timer4):
//   probka k = t_r + 5500,2 + 1000,1*k us, k = 0..6 (b6..b0).
//
// Bit na wykresie = okno o szerokosci jednego okresu odbiornika wysrodkowane
// na probce (ostatni bit response konczy sie na probce). Preambula = od
// zbocza w dol do poczatku okna 1. probki. Potem IDLE.

namespace {
    struct RxModel {
        double first_us;   // t_r -> pierwsza probka
        double period_us;  // odstep probek
        int    nbits;      // liczba probek
    };

    const double  K4_BIT_US             = 476.1;
    const RxModel K4_FRAME_RX           = { 1.5 * K4_BIT_US, K4_BIT_US, 24 };   // ramka K*
    const RxModel K4FP_RX               = { 5500.2, 1000.1, 7 };                // ramka KPA*500
    const double  K4_TAIL_SEARCH_END_US = 17 * K4_BIT_US + 4400.0;

    // Preambula radia: LOW 1896 us; poza tym zakresem ramka dostaje ostrzezenie
    const double K4_PRE_LOW_MIN_US = 1600.0;
    const double K4_PRE_LOW_MAX_US = 2200.0;

    // Zbocze blizej probki niz ten ulamek okresu = ostrzezenie
    const double EDGE_MARGIN_FRACTION = 0.10;
}

AUXBUSAnalyzer::AUXBUSAnalyzer() : Analyzer2(), mAUXBUS( nullptr ) {
    SetAnalyzerSettings( &mSettings );
    UseFrameV2();
}

AUXBUSAnalyzer::~AUXBUSAnalyzer() {
    KillThread();
}

void AUXBUSAnalyzer::SetupResults() {
    mResults.reset( new AUXBUSAnalyzerResults( this, &mSettings ) );
    SetAnalyzerResults( mResults.get() );
    mResults->AddChannelBubblesWillAppearOn( mSettings.mChannel );
}

void AUXBUSAnalyzer::WorkerThread() {
    mAUXBUS = GetAnalyzerChannelData( mSettings.mChannel );

    const double per_us = (double)GetSampleRate() / 1e6;
    auto us2s = [&]( double us ) { return (S64)( us * per_us + 0.5 ); };

    // Start w trakcie LOW: przejdz do HIGH; potem do zbocza w dol
    if( mAUXBUS->GetBitState() == BIT_LOW )
        mAUXBUS->AdvanceToNextEdge();
    mAUXBUS->AdvanceToNextEdge();

    U64  idle_from      = 0;
    bool have_idle_from = false;
    bool pending_low    = false;   // ramka zaczyna sie od biezacej pozycji (linia juz LOW)
    bool pending_tail   = false;   // ... i jest to ogon response po oknie K4FP
    U64  tail_deadline  = 0;       // zbocze w dol przed tym miejscem = ogon response

    while( true ) {
        // --- zbocze w dol (albo poczatek nasluchu przy LOW): poczatek ramki ---
        U64 t_f = mAUXBUS->GetSampleNumber();
        // ogon response: linia LOW po oknie nasluchu K4FP. K4FP wraca wtedy do
        // bezczynnosci, widzi LOW i odbiera to jak ramke wzmacniacza.
        const bool tail = pending_tail || ( tail_deadline && t_f < tail_deadline );
        tail_deadline = 0;
        U8  collision_flag = ( pending_low && !tail ) ? DISPLAY_AS_ERROR_FLAG : 0;
        pending_low = pending_tail = false;

        if( have_idle_from && t_f > idle_from ) {
            FrameV2 iv2;
            mResults->AddFrameV2( iv2, "idle", idle_from, t_f - 1 );
            Frame ifr;
            ifr.mStartingSampleInclusive = (S64)idle_from;
            ifr.mEndingSampleInclusive   = (S64)( t_f - 1 );
            ifr.mType  = FRAME_TYPE_IDLE;
            ifr.mData1 = 0;
            ifr.mFlags = 0;
            mResults->AddFrame( ifr );
            mResults->CommitResults();
        }

        // --- zbocze w gore: od niego liczy odbiornik ---
        mAUXBUS->AdvanceToNextEdge();
        U64    t_r    = mAUXBUS->GetSampleNumber();
        double low_us = (double)( t_r - t_f ) / per_us;

        bool           amp     = tail || low_us >= (double)mSettings.mAmpLowThresholdUs;
        const RxModel& rx      = amp ? K4FP_RX : K4_FRAME_RX;
        const double   half_us = rx.period_us / 2.0;
        const U64      dir     = amp ? DIR_AMP : DIR_K4;

        U8 pre_flags = collision_flag | ( tail ? DISPLAY_AS_WARNING_FLAG : 0 );
        if( !amp && ( low_us < K4_PRE_LOW_MIN_US || low_us > K4_PRE_LOW_MAX_US ) )
            pre_flags |= DISPLAY_AS_WARNING_FLAG;

        U64 first_cell = t_r + us2s( rx.first_us - half_us );

        {
            FrameV2 fv2;
            fv2.AddString( "dir", amp ? "KPA*500" : "K*" );
            fv2.AddInteger( "low_us", (S64)( low_us + 0.5 ) );
            fv2.AddByte( "nbits", (U8)rx.nbits );
            fv2.AddBoolean( "collision", collision_flag != 0 );
            fv2.AddBoolean( "tail", tail );
            mResults->AddFrameV2( fv2, "preamble", t_f, first_cell - 1 );

            Frame fr;
            fr.mStartingSampleInclusive = (S64)t_f;
            fr.mEndingSampleInclusive   = (S64)( first_cell - 1 );
            fr.mType  = FRAME_TYPE_PREAMBLE;
            fr.mData1 = (U64)( low_us + 0.5 );
            fr.mData2 = dir;
            fr.mFlags = pre_flags;
            mResults->AddFrame( fr );
            mResults->AddMarker( t_r, AnalyzerResults::UpArrow, mSettings.mChannel );
        }
        mResults->CommitResults();
        ReportProgress( first_cell );
        CheckIfThreadShouldExit();

        // --- probki odbiornika ---
        // K*: jesli pierwszy bajt nie jest Frequency (0x40-0x7F), probki 16..23
        // to response odbierany przez K4FP (ta sama siatka 476,1 us).
        U64  frame_end = first_cell;
        U8   first_byte = 0;
        bool cmd_frame = false;
        for( int k = 0; k < rx.nbits; k++ ) {
            bool   resp      = !amp && k >= 16 && cmd_frame;
            double period    = rx.period_us;
            double s_us      = rx.first_us + rx.period_us * k;
            U64    s         = t_r + us2s( s_us );
            U64    cell_from = t_r + us2s( s_us - period / 2.0 );
            U64    cell_to   = t_r + us2s( s_us + period / 2.0 ) - 1;
            if( resp && k == 23 )                    // K4FP konczy nasluch na tej probce
                cell_to = s;
            U64    margin    = (U64)us2s( period * EDGE_MARGIN_FRACTION );

            // zbocze w odleglosci < margin od probki? (pozycje rosna monotonicznie)
            mAUXBUS->AdvanceToAbsPosition( s - margin );
            bool near_edge = mAUXBUS->WouldAdvancingCauseTransition( (U32)( 2 * margin ) );

            mAUXBUS->AdvanceToAbsPosition( s );
            U8 v = mAUXBUS->GetBitState() == BIT_HIGH ? 1 : 0;
            if( k < 8 )
                first_byte = (U8)( ( first_byte << 1 ) | v );
            if( k == 7 )
                cmd_frame = !( first_byte >= 0x40 && first_byte <= 0x7F );

            FrameV2 fv2;
            fv2.AddByte( "value", v );
            fv2.AddByte( "index", (U8)k );
            fv2.AddString( "dir", amp ? "KPA*500" : "K*" );
            fv2.AddBoolean( "near_edge", near_edge );
            fv2.AddString( "rx", ( amp || resp ) ? "K4FP" : "KPA*500" );
            mResults->AddFrameV2( fv2, "bit", cell_from, cell_to );

            Frame fr;
            fr.mStartingSampleInclusive = (S64)cell_from;
            fr.mEndingSampleInclusive   = (S64)cell_to;
            fr.mType  = FRAME_TYPE_BIT;
            fr.mData1 = v;
            fr.mData2 = ( (U64)k << 8 ) | dir;
            fr.mFlags = near_edge ? DISPLAY_AS_WARNING_FLAG : 0;
            mResults->AddFrame( fr );

            AnalyzerResults::MarkerType mk =
                near_edge ? AnalyzerResults::ErrorDot : ( v ? AnalyzerResults::One : AnalyzerResults::Zero );
            mResults->AddMarker( s, mk, mSettings.mChannel );

            mResults->CommitResults();
            ReportProgress( cell_to );
            CheckIfThreadShouldExit();
            frame_end = cell_to;
        }

        // --- koniec probkowania: od tego miejsca idle ---
        idle_from      = frame_end + 1;
        have_idle_from = true;

        mAUXBUS->AdvanceToAbsPosition( idle_from );
        if( mAUXBUS->GetBitState() == BIT_LOW && !( !amp && cmd_frame ) && mAUXBUS->GetSampleOfNextEdge() - idle_from < (U64)us2s( rx.period_us / 2.0 ) ) {
            // ostatni bit LOW nadajnika trwa odrobine dluzej niz okno ostatniej
            // probki (zegar nadajnika) - to koniec ramki, nie nowa ramka
            mAUXBUS->AdvanceToNextEdge();
            idle_from = mAUXBUS->GetSampleNumber();
        }
        if( mAUXBUS->GetBitState() == BIT_LOW ) {
            pending_tail = !amp && cmd_frame;
            // Linia nadal LOW po koncu probkowania (np. wzmacniacz zaczal LOW
            // w trakcie ramki K4). Odbiornik wrocil do nasluchu, widzi LOW
            // i czeka na HIGH - nowa ramka zaczyna sie dla niego tutaj.
            pending_low = true;
            continue;
        }
        if( !amp && cmd_frame )
            tail_deadline = t_r + us2s( K4_TAIL_SEARCH_END_US );
        mAUXBUS->AdvanceToNextEdge();   // zbocze w dol nastepnej ramki (na koncu danych blokuje)
        CheckIfThreadShouldExit();
    }
}

U32 AUXBUSAnalyzer::GenerateSimulationData( U64, U32, SimulationChannelDescriptor** ) {
    return 0;
}

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName() {
    return "Elecraft AUXBUS LLA";
}

extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer() {
    return new AUXBUSAnalyzer();
}

extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer ) {
    delete analyzer;
}
