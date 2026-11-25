#pragma once
#include <Analyzer.h>
#include "AUXBUSAnalyzerSettings.h"
#include "AUXBUSAnalyzerResults.h"

class AUXBUSAnalyzer : public Analyzer2 {
public:
    AUXBUSAnalyzer();
    virtual ~AUXBUSAnalyzer();

    virtual void SetupResults() override;
    virtual void WorkerThread() override;

    virtual U32  GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels ) override;
    virtual U32  GetMinimumSampleRateHz() override { return 10000; }
    virtual const char* GetAnalyzerName() const override { return "Elecraft AUXBUS LLA"; }
    virtual bool NeedsRerun() override { return false; }

protected:
    AUXBUSAnalyzerSettings  mSettings;
    std::unique_ptr<AUXBUSAnalyzerResults> mResults;
    AnalyzerChannelData*    mAUXBUS;
};
