// This code was auto-generated. Do not modify it.

#include "Engine/Scripting/BinaryModule.h"
#include "OnlinePlatformXboxLive.Gen.h"

StaticallyLinkedBinaryModuleInitializer StaticallyLinkedBinaryModuleOnlinePlatformXboxLive(GetBinaryModuleOnlinePlatformXboxLive);

extern "C" BinaryModule* GetBinaryModuleOnlinePlatformXboxLive()
{
    static NativeBinaryModule module("OnlinePlatformXboxLive", MAssemblyOptions());
    return &module;
}
