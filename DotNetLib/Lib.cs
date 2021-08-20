// ReSharper disable UnusedType.Global

using System;
using System.Runtime.InteropServices;

namespace DotNetLib
{
    [StructLayout(LayoutKind.Sequential)]
    public readonly unsafe struct HostApi
    {
        public readonly delegate*<void> Hello;
    }

    public static class Lib
    {
        // ReSharper disable once UnusedMember.Global
        [UnmanagedCallersOnly]
        public static void PluginMain(HostApi hostApi)
        {
            unsafe
            {
                hostApi.Hello();
            }

            Console.WriteLine("Hello from managed assembly!");
        }
    }
}
