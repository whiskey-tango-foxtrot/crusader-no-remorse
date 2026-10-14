// Screen presentation driven by usecode: palette fades (PaletteFaderProcess) and cutscene movies (MovieGump).
// The engine only keeps the state; the game layer (HUD / media player) renders it.
#pragma once
#include "CruCore.h"
#include "Kernel.h"

// Fades FCruEngine::FadeAlpha from its current value to Target over Steps ticks (PaletteFaderProcess).
class FFadeProcess : public FProcess
{
public:
    FFadeProcess(float InFrom, float InTo, int32 InSteps);
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("FadeProcess"); }
private:
    float From, To; int32 Steps, Counter = 0;
};

// Lives while the game layer plays a cutscene; the kernel is paused meanwhile (the original uses a modal gump).
class FMovieProcess : public FProcess
{
public:
    FMovieProcess() : FProcess(0, 0) {}
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("MovieProcess"); }
};
