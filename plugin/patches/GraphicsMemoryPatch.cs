using HarmonyLib;

namespace DiscoLinuxPort
{
    // Correct SystemInfo.graphicsMemorySize globally (Mesa/GL reports a
    // moving free-memory reading; libPortGfxInfo.so makes the query the
    // engine fails to make — see PortGfxInfo.cs). One patch point covers
    // the game's own heuristics and AmplifyTexture's streaming budget, so
    // AmplifyTexture.dll stays byte-identical.
    internal static class GraphicsMemoryPatch
    {
        public static bool Apply(Harmony harmony) =>
            PatchApplier.Apply(harmony, "UnityEngine.SystemInfo", "get_graphicsMemorySize", typeof(GraphicsMemoryPatch), prefix: nameof(Prefix));

        private static bool Prefix(ref int __result)
        {
            __result = PortGfxInfo.VideoMemoryMB;
            return false;
        }
    }
}
