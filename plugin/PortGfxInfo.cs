using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace DiscoLinuxPort
{
    // Video-memory helper: libPortGfxInfo.so makes the GLX query the engine
    // fails to make; falls back to the engine's own value when unavailable.
    // Consumed by GraphicsMemoryPatch.
    internal static class PortGfxInfo
    {
        [DllImport("PortGfxInfo")]
        private static extern int port_gfx_video_memory_mb();

        [DllImport("PortGfxInfo")]
        private static extern int port_gfx_unified_memory();

        private static int engineValue;
        private static bool queried;
        private static int effective;

        internal static void CaptureEngineValue(int value)
        {
            engineValue = value;
        }

        public static int VideoMemoryMB
        {
            get
            {
                if (!queried)
                {
                    queried = true;
                    effective = engineValue;
                    try
                    {
                        int advertised = port_gfx_video_memory_mb();
                        int unified = port_gfx_unified_memory();
                        if (advertised > 0)
                        {
                            int used = advertised;
                            if (unified == 1 && SystemInfo.systemMemorySize > used)
                            {
                                // Shared-memory GPU: the aperture is not a hard
                                // limit, textures live in the system pool.
                                used = SystemInfo.systemMemorySize;
                            }
                            Debug.Log("[PortGfxInfo] GL video memory " + advertised + " MB (unified=" + unified + "), engine reported " + engineValue + " MB -> using " + used + " MB");
                            effective = used;
                        }
                        else
                        {
                            Debug.Log("[PortGfxInfo] native query unavailable; using engine value " + effective + " MB");
                        }
                    }
                    catch (Exception e)
                    {
                        Debug.LogWarning("[PortGfxInfo] plugin call failed (" + e.GetType().Name + "); using engine value " + effective + " MB");
                    }
                }
                return effective;
            }
        }
    }
}
