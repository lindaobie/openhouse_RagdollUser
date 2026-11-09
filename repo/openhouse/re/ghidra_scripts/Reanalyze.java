import ghidra.app.script.GhidraScript;
import ghidra.app.plugin.core.analysis.AutoAnalysisManager;
public class Reanalyze extends GhidraScript {
  public void run() throws Exception {
    AutoAnalysisManager m = AutoAnalysisManager.getAnalysisManager(currentProgram);
    m.reAnalyzeAll(null);
    m.startAnalysis(monitor);
    println("REANALYZED fns="+currentProgram.getFunctionManager().getFunctionCount());
  }
}
