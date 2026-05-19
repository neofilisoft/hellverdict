using UnityEngine;
using System.Runtime.InteropServices;

namespace BalmungDoom.Core
{
    public static class NativePlugin
    {
        #if UNITY_STANDALONE_WIN
            private const string DLL_NAME = "DoomCore";
        #else
            private const string DLL_NAME = "libDoomCore";
        #endif

        public struct RaycastResult
        {
            public bool hit;
            public float distance;
            public Vector3 point;
            public Vector3 normal;
        }

        [DllImport(DLL_NAME)]
        private static extern void InitializeDoomCore();

        [DllImport(DLL_NAME)]
        private static extern void ShutdownDoomCore();

        [DllImport(DLL_NAME, EntryPoint = "UpdatePhysics")]
        private static extern void UpdatePhysicsNative(float deltaTime);

        [DllImport(DLL_NAME)]
        private static extern void ProcessRaycast(float[] origin, float[] direction, float[] hit);

        [DllImport(DLL_NAME)]
        private static extern void ProcessCollision(int objectA, int objectB);

        [DllImport(DLL_NAME, EntryPoint = "GetPerformanceMetrics")]
        private static extern float GetPerformanceMetricsNative();

        [DllImport(DLL_NAME, EntryPoint = "OptimizeRenderer")]
        private static extern void OptimizeRendererNative();

        private static bool nativeAvailable;

        public static void Initialize()
        {
            try
            {
                InitializeDoomCore();
                nativeAvailable = true;
                Debug.Log("DoomCore native plugin initialized successfully");
            }
            catch (System.Exception ex) when (ex is System.DllNotFoundException || ex is System.EntryPointNotFoundException)
            {
                nativeAvailable = false;
                Debug.LogWarning("DoomCore plugin not found - running in managed mode");
            }
        }

        public static void Shutdown()
        {
            try
            {
                if (nativeAvailable)
                    ShutdownDoomCore();
                Debug.Log("DoomCore native plugin shutdown");
            }
            catch (System.Exception ex) when (ex is System.DllNotFoundException || ex is System.EntryPointNotFoundException)
            {
                Debug.LogWarning("DoomCore plugin not found");
            }
        }

        public static void InitPhysics()
        {
            Initialize();
        }

        public static void ShutdownPhysics()
        {
            Shutdown();
        }

        public static void UpdatePhysics(float deltaTime)
        {
            if (!nativeAvailable)
                return;

            try
            {
                UpdatePhysicsNative(deltaTime);
            }
            catch (System.Exception ex) when (ex is System.DllNotFoundException || ex is System.EntryPointNotFoundException)
            {
                nativeAvailable = false;
            }
        }

        public static void AddWall(float x, float y, float z, float width, float height, float depth)
        {
        }

        public static RaycastResult CastRay(float x, float y, float z, float dx, float dy, float dz)
        {
            if (Physics.Raycast(new Vector3(x, y, z), new Vector3(dx, dy, dz), out RaycastHit hit, 100f))
            {
                return new RaycastResult
                {
                    hit = true,
                    distance = hit.distance,
                    point = hit.point,
                    normal = hit.normal
                };
            }

            return default;
        }

        public static float GetPerformanceMetrics()
        {
            return Time.smoothDeltaTime > 0f ? 1f / Time.smoothDeltaTime : 0f;
        }

        public static void OptimizeRenderer()
        {
        }
    }
}
