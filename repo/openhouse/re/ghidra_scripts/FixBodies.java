import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.nio.file.*;
import java.util.*;
// For each .eh_frame FDE range, make sure the whole range is disassembled and the
// function body covers it; clear wrong noreturn on functions called inside.
public class FixBodies extends GhidraScript {
  public void run() throws Exception {
    FunctionManager fm=currentProgram.getFunctionManager(); Listing L=currentProgram.getListing();
    int fixed=0, nr=0; long base=currentProgram.getImageBase().getOffset();
    for(int pass=0;pass<3;pass++){
      int changed=0;
      for(String l: Files.readAllLines(Paths.get(getScriptArgs()[0]))){
        String[] p=l.split("\\.\\."); long s=Long.parseUnsignedLong(p[0],16)+base, e=Long.parseUnsignedLong(p[1],16)+base;
        Address a=toAddr(s); Function f=fm.getFunctionAt(a);
        if(f==null) continue;
        long covered=0; for(AddressRange r: f.getBody()) covered+=r.getLength();
        if(covered >= (e-s)*8/10) continue;
        // walk range: disassemble undefined spots, clear noreturn of call targets, CALL_RETURN overrides
        Address cur=a; Address end=toAddr(e);
        while(cur.compareTo(end)<0){
          Instruction ins=L.getInstructionAt(cur);
          if(ins==null){
            if(L.getDefinedDataAt(cur)!=null){ cur=cur.add(1); continue; }
            new DisassembleCommand(cur,null,true).applyTo(currentProgram,monitor);
            ins=L.getInstructionAt(cur);
            if(ins==null){ cur=cur.add(1); continue; }
          }
          if(ins.getFlowType().isCall()){
            for(Address t: ins.getFlows()){ Function g=fm.getFunctionAt(t); if(g!=null && g.hasNoReturn() && !g.getName().matches(".*(throw|abort|Unwind_Resume|exit|terminate|stack_chk|assert).*")){ g.setNoReturn(false); nr++; } }
            if(ins.getFlowOverride()==ghidra.program.model.listing.FlowOverride.CALL_RETURN) ins.setFlowOverride(ghidra.program.model.listing.FlowOverride.NONE);
          }
          cur=ins.getMaxAddress().next();
        }
        CreateFunctionCmd.fixupFunctionBody(currentProgram,f,monitor);
        changed++;
      }
      fixed+=changed; if(changed==0) break;
    }
    println("FIXBODIES fixed="+fixed+" noret_cleared="+nr);
  }
}
