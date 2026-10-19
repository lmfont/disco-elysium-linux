using System;
using System.Reflection;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using UnityEngine;

// Disco Elysium Linux port — runtime patch plugin (BepInEx 5 / HarmonyX).
//
// The game's files are never modified: every platform delta is applied in
// memory at startup. The plugin is reflection-based on purpose — it needs no
// game assemblies at build time and resolves each target by name at runtime,
// logging and skipping anything it cannot find (e.g. on an unknown game
// build).
//
// Each patch lives in its own file under patches/:
//
//   patches/WarmupPatch.cs              skip Shader.WarmupAllShaders()
//   patches/StencilPatch.cs             drop the D3D-only stencil format
//   patches/GraphicsMemoryPatch.cs      corrected Mesa/GL video-memory value
//   patches/HighlightGuardPatch.cs      teardown null guard
//   patches/AssetBundlePlatformPatch.cs use the shipped AssetBundles/Windows
//   patches/InternalIdPatch.cs          normalize catalog separators
//
// The native shims (libAmplifyTextureNative.so, libPortGfxInfo.so) are found
// via LD_LIBRARY_PATH, which run.sh points at shims/.

namespace DiscoLinuxPort
{
    [BepInPlugin(PluginGuid, PluginName, PortVersion.Value)]
    public class PortRuntimePlugin : BaseUnityPlugin
    {
        public const string PluginGuid = "disco.linux.port.runtime";
        public const string PluginName = "Disco Elysium Linux port runtime";

        internal static ManualLogSource Log;

        private readonly struct PatchEntry
        {
            public readonly string Name;
            public readonly Func<Harmony, bool> Apply;

            public PatchEntry(string name, Func<Harmony, bool> apply)
            {
                Name = name;
                Apply = apply;
            }
        }

        private static readonly PatchEntry[] PatchList =
        {
            new PatchEntry("warmup", WarmupPatch.Apply),
            new PatchEntry("stencil", StencilPatch.Apply),
            new PatchEntry("graphics-memory", GraphicsMemoryPatch.Apply),
            new PatchEntry("highlight-guard", HighlightGuardPatch.Apply),
            new PatchEntry("assetbundles", AssetBundlePlatformPatch.Apply),
            new PatchEntry("addressables-paths", InternalIdPatch.Apply),
        };

        private void Awake()
        {
            Log = Logger;

            // Capture the engine's own value before the getter is patched.
            PortGfxInfo.CaptureEngineValue(SystemInfo.graphicsMemorySize);

            // Which AT2 readback path the game will take (native on OpenGL;
            // the shim implements it). Read by reflection: the compile-time
            // UnityEngine stub predates this property.
            bool asyncReadback = false;
            var prop = typeof(SystemInfo).GetProperty("supportsAsyncGPUReadback",
                BindingFlags.Public | BindingFlags.Static);
            if (prop != null)
                asyncReadback = (bool)prop.GetValue(null);
            Logger.LogInfo($"platform: renderer={SystemInfo.graphicsDeviceType}, AsyncGPUReadback={asyncReadback}");

            // Game assemblies are loaded lazily by the engine; force them so
            // the by-name lookups in the patches can see their types.
            foreach (string assemblyName in new[] { "Assembly-CSharp", "Unity.ResourceManager", "Unity.Addressables" })
            {
                try { Assembly.Load(assemblyName); }
                catch (Exception e) { Log.LogWarning($"could not preload {assemblyName}: {e.Message}"); }
            }

            var harmony = new Harmony(PluginGuid);
            int applied = 0;
            foreach (var patch in PatchList)
            {
                try
                {
                    if (patch.Apply(harmony))
                        applied++;
                }
                catch (Exception e)
                {
                    Log.LogError($"patch '{patch.Name}' threw: {e}");
                }
            }

            Logger.LogInfo($"port runtime ready: {applied}/{PatchList.Length} patches applied");
        }
    }
}
