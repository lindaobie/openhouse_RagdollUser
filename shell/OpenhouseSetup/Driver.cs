using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace OpenhouseSetup;

/// Driver install / restore into SteamVR. The rules are the ones the working installer
/// settled on: never replace a running driver, keep Valve's DLL as .stock forever.
internal static class Driver
{
    private static readonly string[] Blocking = { "steam", "vrserver", "vrmonitor", "vrcompositor", "monado-service" };

    public static string? SteamVrPath()
    {
        var pathsFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "openvr", "openvrpaths.vrpath");
        if (File.Exists(pathsFile))
        {
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(pathsFile));
                if (doc.RootElement.TryGetProperty("runtime", out var rt) &&
                    rt.ValueKind == JsonValueKind.Array && rt.GetArrayLength() > 0)
                {
                    var first = rt[0].GetString();
                    if (!string.IsNullOrWhiteSpace(first) && Directory.Exists(first)) return first;
                }
            }
            catch { }
        }
        var steam = Microsoft.Win32.Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
        if (!string.IsNullOrEmpty(steam))
        {
            var candidate = Path.Combine(steam, "steamapps", "common", "SteamVR");
            if (Directory.Exists(candidate)) return candidate;
        }
        return null;
    }

    public static string DllPath(string root) =>
        Path.Combine(root, "drivers", "lighthouse", "bin", "win64", "driver_lighthouse.dll");

    public static bool IsOurs(string dll)
    {
        try
        {
            // Our build always carries this log string; Valve's never does.
            var text = Encoding.ASCII.GetString(File.ReadAllBytes(dll));
            return text.Contains("liblh session started");
        }
        catch { return false; }
    }

    public static List<string> Running()
    {
        var found = new List<string>();
        foreach (var name in Blocking)
            try { if (Process.GetProcessesByName(name).Length > 0) found.Add(name); } catch { }
        return found;
    }

    public static void StopAll(Action<string, string> emit)
    {
        foreach (var name in Blocking)
        {
            Process[] list;
            try { list = Process.GetProcessesByName(name); } catch { continue; }
            if (list.Length == 0) continue;
            emit($"结束 {name}…", "warn");
            foreach (var p in list)
            {
                try { p.Kill(entireProcessTree: true); p.WaitForExit(5000); } catch { }
            }
        }
        Thread.Sleep(800);
    }

    public static string? PackageFile(string name)
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 6 && dir is not null; i++, dir = dir.Parent)
        {
            var p = Path.Combine(dir.FullName, "packages", "driver", "openhouse-windows-x64", name);
            if (File.Exists(p)) return p;
        }
        return null;
    }

    public static void Install(Action<string, string> emit)
    {
        var root = SteamVrPath();
        if (root is null) { emit("找不到 SteamVR", "warn"); return; }
        emit($"SteamVR: {root}", "");

        var running = Running();
        if (running.Count > 0) StopAll(emit);

        var source = PackageFile("driver_lighthouse.dll");
        if (source is null) { emit("驱动包里没有 driver_lighthouse.dll", "warn"); return; }

        var target = DllPath(root);
        var dir = Path.GetDirectoryName(target)!;
        if (!Directory.Exists(dir)) { emit("SteamVR 里没有 lighthouse 驱动目录，先启动一次 SteamVR", "warn"); return; }

        var backup = target + ".stock";
        if (File.Exists(target) && !IsOurs(target))
        {
            File.Copy(target, backup, overwrite: true);
            emit($"已把 SteamVR 原驱动存为 driver_lighthouse.dll.stock", "ok");
        }
        else if (!File.Exists(backup))
        {
            emit("装的已经是 openhouse 驱动，但没有 .stock 备份：无法安全回退，先验证 SteamVR 文件完整性再装", "warn");
            return;
        }

        var staging = target + ".new";
        File.Copy(source, staging, overwrite: true);
        File.Move(staging, target, overwrite: true);
        emit("已写入驱动", "ok");

        var a = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(source)));
        var b = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(target)));
        if (a != b) { emit("哈希不一致，安装失败", "warn"); return; }
        emit("哈希校验通过", "ok");

        // The driver's own settings page inside SteamVR.
        var schemaSource = PackageFile("settingsschema.vrsettings");
        if (schemaSource is not null)
        {
            var schema = Path.Combine(root, "drivers", "lighthouse", "resources", "settings", "settingsschema.vrsettings");
            Directory.CreateDirectory(Path.GetDirectoryName(schema)!);
            if (File.Exists(schema) && !File.ReadAllText(schema).Contains("driver_openhouse"))
                File.Copy(schema, schema + ".stock", overwrite: true);
            File.Copy(schemaSource, schema, overwrite: true);
            emit("已安装设置页", "ok");
        }

        emit("安装完成。启动 SteamVR。", "ok");
    }

    public static void Uninstall(Action<string, string> emit)
    {
        var root = SteamVrPath();
        if (root is null) { emit("找不到 SteamVR", "warn"); return; }

        if (Running().Count > 0) StopAll(emit);

        var schema = Path.Combine(root, "drivers", "lighthouse", "resources", "settings", "settingsschema.vrsettings");
        if (File.Exists(schema) && File.ReadAllText(schema).Contains("driver_openhouse"))
        {
            File.Delete(schema);
            emit("已移除设置页", "ok");
        }
        if (File.Exists(schema + ".stock")) File.Move(schema + ".stock", schema, overwrite: true);

        var target = DllPath(root);
        var backup = target + ".stock";
        if (!File.Exists(backup)) { emit("没有原厂备份，没法还原", "warn"); return; }
        File.Move(backup, target, overwrite: true);
        emit("已还原 SteamVR 原驱动", "ok");
    }
}
