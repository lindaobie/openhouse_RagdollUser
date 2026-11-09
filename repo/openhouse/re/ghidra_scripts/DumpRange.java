import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class DumpRange extends GhidraScript {
  public void run() throws Exception {
    String[] a=getScriptArgs(); String out=a[0]; long lo=Long.parseLong(a[1],16), hi=Long.parseLong(a[2],16);
    new File(out).mkdirs();
    DecompInterface d=new DecompInterface(); d.openProgram(currentProgram);
    PrintWriter idx=new PrintWriter(new FileWriter(out+"/index.tsv",true));
    for(Function f: currentProgram.getFunctionManager().getFunctions(toAddr(lo),true)){
      long e=f.getEntryPoint().getOffset(); if(e>=hi) break; if(f.isThunk()) continue;
      String s=f.getEntryPoint().toString();
      idx.println(s+"\t"+f.getBody().getNumAddresses()+"\t"+f.getName(true));
      DecompileResults r=d.decompileFunction(f,90,monitor);
      String c=(r!=null&&r.decompileCompleted())?r.getDecompiledFunction().getC():"// FAILED";
      try(PrintWriter w=new PrintWriter(new FileWriter(out+"/"+s+".c"))){ w.println("// "+f.getName(true)+" @ "+s); w.print(c);}
    }
    idx.close();
  }
}
