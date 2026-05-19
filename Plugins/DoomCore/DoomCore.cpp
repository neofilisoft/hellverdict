#include "DoomCore.h"
#include <cmath>

namespace DoomCore
{
    static bool initialized = false;
    static float performanceMetrics = 60.0f;

    void InitializeDoomCore()
    {
        initialized = true;
    }

    void ShutdownDoomCore()
    {
        initialized = false;
    }

    void UpdatePhysics(float deltaTime)
    {
        if (!initialized) return;
    }

    void ProcessRaycast(float* origin, float* direction, float* hit)
    {
        if (!origin || !direction || !hit) return;

        hit[0] = origin[0] + direction[0];
        hit[1] = origin[1] + direction[1];
        hit[2] = origin[2] + direction[2];
    }

    void ProcessCollision(int objectA, int objectB)
    {
        if (!initialized) return;
    }

    float GetPerformanceMetrics()
    {
        return performanceMetrics;
    }

    void OptimizeRenderer()
    {
        performanceMetrics = 60.0f;
    }
}

extern "C"
{
    void InitializeDoomCore()
    {
        DoomCore::InitializeDoomCore();
    }

    void ShutdownDoomCore()
    {
        DoomCore::ShutdownDoomCore();
    }

    void UpdatePhysics(float deltaTime)
    {
        DoomCore::UpdatePhysics(deltaTime);
    }

    void ProcessRaycast(float* origin, float* direction, float* hit)
    {
        DoomCore::ProcessRaycast(origin, direction, hit);
    }

    void ProcessCollision(int objectA, int objectB)
    {
        DoomCore::ProcessCollision(objectA, objectB);
    }

    float GetPerformanceMetrics()
    {
        return DoomCore::GetPerformanceMetrics();
    }

    void OptimizeRenderer()
    {
        DoomCore::OptimizeRenderer();
    }
}
