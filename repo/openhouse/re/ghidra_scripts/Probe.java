import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
public class Probe extends GhidraScript {
  public void run() throws Exception {
    for(String s: getScriptArgs()){
      Instruction i=getInstructionAt(toAddr(Long.parseLong(s,16)));
      Function f=getFunctionAt(toAddr(Long.parseLong(s,16)));
      if(f!=null) println("PROBE fn "+f.getName()+" noret="+f.hasNoReturn()+" thunk="+f.isThunk()+(f.isThunk()?" -> "+f.getThunkedFunction(false).getName()+" noret="+f.getThunkedFunction(false).hasNoReturn():"")+" body="+f.getBody());
      println("PROBE ins "+s+" "+i+(i==null?"":" flow="+i.getFlowType()+" ovr="+i.getFlowOverride()+" ft="+i.getFallThrough()));
    }
  }
}
