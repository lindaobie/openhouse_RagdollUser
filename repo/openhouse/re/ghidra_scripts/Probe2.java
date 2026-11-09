import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
public class Probe2 extends GhidraScript {
  public void run() throws Exception {
    for(String s: getScriptArgs()){
      var a=toAddr(Long.parseLong(s,16));
      Function f=getFunctionContaining(a);
      println("PROBE2 "+s+" in="+(f==null?"none":f.getName()+"@"+f.getEntryPoint())+" data="+getDataAt(a)+" cu="+currentProgram.getListing().getCodeUnitAt(a));
    }
  }
}
