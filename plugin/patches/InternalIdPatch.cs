using HarmonyLib;

namespace DiscoLinuxPort
{
    // The shipped Addressables catalog stores Windows separators
    // (…\StandaloneWindows64\….bundle). Normalize every internal id in
    // memory; catalog.json and the bundle folders stay untouched.
    internal static class InternalIdPatch
    {
        public static bool Apply(Harmony harmony) =>
            PatchApplier.Apply(harmony, "UnityEngine.ResourceManagement.ResourceManager", "TransformInternalId",
                typeof(InternalIdPatch), postfix: nameof(Postfix));

        private static void Postfix(ref string __result)
        {
            if (!string.IsNullOrEmpty(__result) && __result.IndexOf('\\') >= 0)
                __result = __result.Replace('\\', '/');
        }
    }
}
