#include "CruPresentation.h"
#include "CruEngine.h"

FFadeProcess::FFadeProcess(float InFrom, float InTo, int32 InSteps) : FProcess(0, 0), From(InFrom), To(InTo), Steps(FMath::Max(1, InSteps)) {}

void FFadeProcess::Run()
{
    if (!GCruEngine) { Terminate(); return; }
    ++Counter;
    GCruEngine->FadeAlpha = FMath::Lerp(From, To, FMath::Clamp((float)Counter / Steps, 0.f, 1.f));
    if (Counter >= Steps) Terminate();
}

void FMovieProcess::Run()
{
    if (!GCruEngine || !GCruEngine->bMoviePlaying) Terminate();
}
