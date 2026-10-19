using System;
using System.Reflection;
using HarmonyLib;

namespace DiscoLinuxPort
{
    // Resolves a patch target by name and applies one patch class to it.
    // Failures are logged and reported as false — the plugin fails soft on
    // unknown game builds instead of refusing to load.
    internal static class PatchApplier
    {
        public static bool Apply(Harmony harmony, string typeName, string methodName, Type patchType,
            string prefix = null, string postfix = null, string transpiler = null)
        {
            try
            {
                var type = AccessTools.TypeByName(typeName);
                if (type == null)
                {
                    PortRuntimePlugin.Log.LogError($"patch skipped, type not found: {typeName}");
                    return false;
                }
                var method = AccessTools.Method(type, methodName);
                if (method == null)
                {
                    PortRuntimePlugin.Log.LogError($"patch skipped, method not found: {typeName}.{methodName}");
                    return false;
                }
                harmony.Patch(method,
                    prefix: HarmonyMethodOf(patchType, prefix),
                    postfix: HarmonyMethodOf(patchType, postfix),
                    transpiler: HarmonyMethodOf(patchType, transpiler));
                PortRuntimePlugin.Log.LogInfo($"patched {typeName}.{methodName}");
                return true;
            }
            catch (Exception e)
            {
                PortRuntimePlugin.Log.LogError($"patch failed for {typeName}.{methodName}: {e}");
                return false;
            }
        }

        private static HarmonyMethod HarmonyMethodOf(Type patchType, string name) =>
            name == null
                ? null
                : new HarmonyMethod(patchType.GetMethod(name, BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public));
    }
}
