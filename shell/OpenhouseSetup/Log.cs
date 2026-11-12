using System.Text;

namespace OpenhouseSetup;

/// A plain text log written next to the exe, one file per run.
///
/// The in-app log pages only live in memory: close the window and whatever went wrong is
/// gone. This keeps the same lines on disk so a failed flash can be reconstructed later.
/// Twenty runs is enough history to cover "it worked yesterday"; older files are removed on
/// startup so nothing accumulates unattended.
internal static class Log
{
    private const int KeepRuns = 20;
    private static readonly object Gate = new();
    private static readonly UTF8Encoding Utf8 = new(encoderShouldEmitUTF8Identifier: false);
    private static string? _path;

    /// Where the current run's log lives, or null when the directory is not writable.
    public static string? Dir { get; private set; }

    public static void Open()
    {
        try
        {
            var dir = Path.Combine(AppContext.BaseDirectory, "logs");
            Directory.CreateDirectory(dir);
            Dir = dir;
            _path = Path.Combine(dir, $"openhouse-{DateTime.Now:yyyyMMdd-HHmmss}.log");
            File.WriteAllText(_path,
                $"openhouse UI\r\n启动 {DateTime.Now:yyyy-MM-dd HH:mm:ss}\r\n" +
                $"目录 {AppContext.BaseDirectory}\r\n\r\n", Utf8);
            Prune(dir);
        }
        catch
        {
            // An install directory that refuses writes is not a reason to fail to start;
            // the app simply runs without a file log.
            _path = null;
            Dir = null;
        }
    }

    public static void Write(string level, string text)
    {
        var path = _path;
        if (path is null) return;
        var line = $"{DateTime.Now:HH:mm:ss.fff} {level,-5} {text}\r\n";
        lock (Gate)
        {
            try { File.AppendAllText(path, line, Utf8); } catch { _path = null; }
        }
    }

    private static void Prune(string dir)
    {
        var old = new DirectoryInfo(dir).GetFiles("openhouse-*.log")
            .OrderByDescending(f => f.Name, StringComparer.Ordinal)
            .Skip(KeepRuns);
        foreach (var f in old)
        {
            try { f.Delete(); } catch { }
        }
    }
}
