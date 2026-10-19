using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace DiscoLinuxPort
{
    // GraphicsFormat.R8_UInt is a D3D-only stencil format; on GL the
    // depth-normals RenderTexture fails to create every frame. Nop the
    // `dup; ldc.i4.s 13; callvirt set_stencilFormat` sequence so the RT
    // keeps its plain D24S8 depth buffer.
    internal static class StencilPatch
    {
        // GraphicsFormat.R8_UInt is 13 in Unity 2020.3. Kept as a literal
        // because the compile-time UnityEngine stub predates GraphicsFormat.
        private const int R8UInt = 13;

        public static bool Apply(Harmony harmony) =>
            PatchApplier.Apply(harmony, "DepthNormalsCamera", "CheckResources", typeof(StencilPatch), transpiler: nameof(Transpiler));

        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var list = new List<CodeInstruction>(instructions);
            var sites = new List<int>();
            for (int i = 2; i < list.Count; i++)
            {
                if (IsStencilFormatStore(list, i))
                    sites.Add(i);
            }

            // Fail closed: silently patching zero sites (or more than the
            // expected one) would make the patch report success while the
            // game keeps the broken render texture setup, and a partial edit
            // of several sites could corrupt unrelated IL.
            if (sites.Count != 1)
            {
                const string message = "expected exactly one stencilFormat store";
                PortRuntimePlugin.Log?.LogError($"stencil transpiler: {message}, found {sites.Count}; patch not applied");
                throw new InvalidOperationException($"DepthNormalsCamera.CheckResources: {message}, found {sites.Count}");
            }

            int site = sites[0];
            for (int i = site - 2; i <= site; i++)
            {
                list[i].opcode = OpCodes.Nop;
                list[i].operand = null;
            }
            PortRuntimePlugin.Log?.LogInfo("stencil transpiler: removed 1 stencilFormat store");
            return list;
        }

        private static bool IsStencilFormatStore(List<CodeInstruction> list, int i)
        {
            if (list[i].opcode != OpCodes.Callvirt || !(list[i].operand is MethodInfo method))
                return false;
            if (method.Name != "set_stencilFormat" || method.ReturnType != typeof(void))
                return false;
            if (method.DeclaringType == null || method.DeclaringType.Name != "RenderTexture")
                return false;
            var parameters = method.GetParameters();
            // GraphicsFormat lived under UnityEngine.Experimental.Rendering
            // in this Unity version; accept the type by name so this does not
            // depend on the namespace spelling.
            if (parameters.Length != 1
                || parameters[0].ParameterType.Name != "GraphicsFormat")
                return false;

            if (list[i - 2].opcode != OpCodes.Dup)
                return false;
            return IsIntConstant(list[i - 1], R8UInt);
        }

        private static bool IsIntConstant(CodeInstruction instruction, int value)
        {
            if (instruction.opcode == OpCodes.Ldc_I4_S)
                return instruction.operand is sbyte small && small == value;
            if (instruction.opcode == OpCodes.Ldc_I4)
                return instruction.operand is int integer && integer == value;
            return false;
        }
    }
}
