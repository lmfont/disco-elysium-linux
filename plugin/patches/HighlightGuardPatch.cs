using System.Reflection;
using HarmonyLib;

namespace DiscoLinuxPort
{
    // Teardown paths (e.g. KimSkinSwapper.OnDestroy at shutdown) can call
    // RefreshHighlightMaterials before Start() assigned `renderers`.
    internal static class HighlightGuardPatch
    {
        private static FieldInfo _renderersField;

        public static bool Apply(Harmony harmony)
        {
            var type = AccessTools.TypeByName("FortressOccident.MouseOverHighlight");
            if (type != null)
                _renderersField = AccessTools.Field(type, "renderers");
            return PatchApplier.Apply(harmony, "FortressOccident.MouseOverHighlight", "RefreshHighlightMaterials",
                typeof(HighlightGuardPatch), prefix: nameof(Prefix));
        }

        private static bool Prefix(object __instance)
        {
            if (_renderersField == null)
                return true;
            if (_renderersField.GetValue(__instance) == null)
            {
                PortRuntimePlugin.Log?.LogInfo("MouseOverHighlight guard: renderers == null, skipping refresh");
                return false;
            }
            return true;
        }
    }
}
