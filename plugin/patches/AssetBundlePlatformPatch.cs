using HarmonyLib;

namespace DiscoLinuxPort
{
    // The shipped StreamingAssets folder is AssetBundles/Windows; its
    // bundles are platform-neutral. Point the loader at the shipped folder
    // instead of renaming anything.
    internal static class AssetBundlePlatformPatch
    {
        public static bool Apply(Harmony harmony) =>
            PatchApplier.Apply(harmony, "AddressablesTools.Utility", "GetPlatformForAssetBundles",
                typeof(AssetBundlePlatformPatch), prefix: nameof(Prefix));

        private static bool Prefix(ref string __result)
        {
            __result = "Windows";
            return false;
        }
    }
}
