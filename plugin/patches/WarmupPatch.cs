using HarmonyLib;

namespace DiscoLinuxPort
{
    // Skip Shader.WarmupAllShaders() — the shipped GL shader blobs already
    // carry the variant space; the call would make the driver compile
    // thousands of programs at startup.
    internal static class WarmupPatch
    {
        public static bool Apply(Harmony harmony) =>
            PatchApplier.Apply(harmony, "ShaderWarmupper", "Warmup", typeof(WarmupPatch), prefix: nameof(Prefix));

        private static bool Prefix()
        {
            return false;
        }
    }
}
