import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class DumpAll extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
    PrintWriter idx = new PrintWriter(new FileWriter(out+"/index.tsv"));
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      if (f.isThunk() || f.isExternal()) continue;
      String a = f.getEntryPoint().toString();
      idx.println(a+"\t"+f.getBody().getNumAddresses()+"\t"+f.getName(true));
      DecompileResults r = d.decompileFunction(f, 90, monitor);
      String c = (r!=null && r.decompileCompleted()) ? r.getDecompiledFunction().getC() : "// FAILED "+(r==null?"":r.getErrorMessage());
      try (PrintWriter w = new PrintWriter(new FileWriter(out+"/"+a+".c"))) { w.println("// "+f.getName(true)+" @ "+a); w.print(c); }
    }
    idx.close();
  }
}
