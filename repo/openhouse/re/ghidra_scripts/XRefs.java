import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class XRefs extends GhidraScript {
  public void run() throws Exception {
    for(String s: getScriptArgs()){
      StringBuilder sb=new StringBuilder();
      for(Reference r: getReferencesTo(toAddr(Long.parseLong(s,16)))){
        Function f=getFunctionContaining(r.getFromAddress());
        sb.append(" ").append(r.getFromAddress()).append("(").append(f==null?"?":f.getEntryPoint().toString()).append(")");
      }
      println("XREF "+s+":"+sb);
    }
  }
}
