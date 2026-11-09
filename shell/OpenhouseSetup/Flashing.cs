using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace OpenhouseSetup;

/// Firmware writing to a base station sitting in ISP mode. The details here are the ones
/// the working PowerShell installer settled on; deviating from them bricks flash contents.
internal static class Flashing
{
    public const int FlashBytes = 131072;   // the whole flash, as the ROM exposes it

    public static string? FindCrp(out string label)
    {
        label = "";
        foreach (var d in DriveInfo.GetDrives())
        {
            if (d.DriveType != DriveType.Removable) continue;
            string l;
            try { l = d.VolumeLabel; } catch { continue; }
            if (!l.Contains("CRP", StringComparison.OrdinalIgnoreCase)) continue;
            if (!File.Exists(Path.Combine(d.Name, "firmware.bin"))) continue;
            label = l;
            return d.Name.TrimEnd('\\');
        }
        return null;
    }

    /// The LPC11U37 boot ROM only starts an image whose first eight words sum to zero.
    public static bool VectorsOk(byte[] img)
    {
        if (img.Length < 32) return false;
        ulong sum = 0;
        for (int i = 0; i < 8; i++) sum = (sum + BitConverter.ToUInt32(img, i * 4)) % 4294967296UL;
        return sum == 0;
    }

    /// A code-read-protect word at 0x2FC locks the USB bootloader for good. Never write one.
    public static bool ReadProtected(byte[] img)
    {
        if (img.Length < 0x300) return false;
        var v = BitConverter.ToUInt32(img, 0x2FC);
        return v != 0xFFFFFFFF && v != 0;
    }

    /// Write in place through the file that is already there: replacing it (drag and drop,
    /// or Create) makes the ROM loader write the wrong region.
    public static void WriteImage(string drive, byte[] image)
    {
        int size = (int)(Math.Ceiling(image.Length / 512.0) * 512);
        var padded = new byte[size];
        Array.Fill(padded, (byte)0xFF);
        Buffer.BlockCopy(image, 0, padded, 0, image.Length);
        using var fs = new FileStream(Path.Combine(drive + "\\", "firmware.bin"),
            FileMode.Open, FileAccess.Write, FileShare.None, 512, FileOptions.WriteThrough);
        fs.Write(padded, 0, padded.Length);
        fs.Flush(true);
    }

    public static string? SerialOf(byte[] flash)
    {
        if (flash.Length < 0x1F060) return null;
        if (Encoding.ASCII.GetString(flash, 0x1F000, 8) != "LHEEDUMP") return null;
        var s = Encoding.ASCII.GetString(flash, 0x1F050, 16).Split('\0')[0].Trim();
        return s.Length == 8 ? s.ToUpperInvariant() : null;
    }

    public static string Sha(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));

    public static bool SamePrefix(byte[] a, byte[] b, int count)
    {
        if (a.Length < count || b.Length < count) return false;
        return a.AsSpan(0, count).SequenceEqual(b.AsSpan(0, count));
    }

    /// Exactly what the working installer does: the shell's own Eject verb, then wait.
    public static void Eject(string drive)
    {
        try
        {
            var shellType = Type.GetTypeFromProgID("Shell.Application");
            if (shellType is not null)
            {
                dynamic shell = Activator.CreateInstance(shellType)!;
                shell.Namespace(17).ParseName(drive).InvokeVerb("Eject");
            }
        }
        catch { }
        Thread.Sleep(2000);
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr sec, uint disposition, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(IntPtr device, uint code, IntPtr inBuf, uint inSize,
        IntPtr outBuf, uint outSize, out uint returned, IntPtr overlapped);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
}
