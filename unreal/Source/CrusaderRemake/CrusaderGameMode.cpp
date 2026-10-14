#include "CrusaderGameMode.h"
#include "SilencerPawn.h"
#include "CrusaderHUD.h"
#include "CrusaderPlayerController.h"

ACrusaderGameMode::ACrusaderGameMode()
{
    DefaultPawnClass = ASilencerPawn::StaticClass();
    HUDClass = ACrusaderHUD::StaticClass();
    PlayerControllerClass = ACrusaderPlayerController::StaticClass();
}
