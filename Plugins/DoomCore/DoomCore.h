#pragma once

#ifdef _WIN32
    #define DOOM_EXPORT __declspec(dllexport)
#else
    #define DOOM_EXPORT
#endif

extern "C"
{
    DOOM_EXPORT void InitializeDoomCore();
    DOOM_EXPORT void ShutdownDoomCore();
    
    DOOM_EXPORT void UpdatePhysics(float deltaTime);
    DOOM_EXPORT void ProcessRaycast(float* origin, float* direction, float* hit);
    DOOM_EXPORT void ProcessCollision(int objectA, int objectB);
    
    DOOM_EXPORT float GetPerformanceMetrics();
    DOOM_EXPORT void OptimizeRenderer();
}
