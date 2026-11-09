import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.*;
public class FixNoRet extends GhidraScript {
  static final Set<String> NORET = new HashSet<>(Arrays.asList("abort","exit","_exit","__cxa_throw","__cxa_rethrow","__cxa_throw_bad_array_new_length","_Unwind_Resume","__stack_chk_fail","__assert_fail","pthread_exit","longjmp","__longjmp_chk","__cxa_bad_cast","__cxa_bad_typeid","__cxa_call_terminate","__cxa_pure_virtual","__cxa_deleted_virtual","__fortify_fail","__chk_fail"));
  public void run() throws Exception {
    FunctionManager fm=currentProgram.getFunctionManager(); Listing L=currentProgram.getListing();
    Set<Address> keep=new HashSet<>();
    for(String l: Files.readAllLines(Paths.get(getScriptArgs()[0]))){
      String[] p=l.split("\t"); Address a=toAddr(Long.parseLong(p[0],16));
      Function f=fm.getFunctionAt(a);
      if(f==null){ new DisassembleCommand(a,null,true).applyTo(currentProgram,monitor); f=createFunction(a,p[1]+"@plt"); }
      if(f==null) continue;
      try{ f.setName(p[1]+"@plt",SourceType.USER_DEFINED);}catch(Exception e){}
      f.setNoReturn(NORET.contains(p[1])); if(NORET.contains(p[1])) keep.add(a);
    }
    for(Function f: fm.getFunctions(true)){
      String n=f.getName();
      boolean real = NORET.contains(n) || n.startsWith("__cxa_throw") || n.equals("__cxa_rethrow") || n.equals("__cxa_bad_cast")||n.equals("__cxa_bad_typeid")||n.equals("__cxa_call_terminate");
      if(real) keep.add(f.getEntryPoint());
    }
    // iterate: a function is genuinely noreturn iff it has no RET and every terminal flow is a call to a noreturn fn / jmp to one
    int cleared=0;
    for(Function f: fm.getFunctions(true)){ if(f.hasNoReturn() && !keep.contains(f.getEntryPoint())){ f.setNoReturn(false); cleared++; } }
    // fix fallthroughs after calls
    int fixed=0;
    for(int pass=0;pass<4;pass++){
      int n=0;
      InstructionIterator it=L.getInstructions(true);
      List<Address> todo=new ArrayList<>();
      while(it.hasNext()){
        Instruction ins=it.next();
        if(!ins.getFlowType().isCall()) continue;
        Address[] fl=ins.getFlows(); boolean nr=false;
        for(Address t: fl){ Function g=fm.getFunctionAt(t); if(g!=null && g.hasNoReturn()) nr=true; }
        if(nr) continue;
        if(ins.getFlowOverride()==FlowOverride.CALL_RETURN) ins.setFlowOverride(FlowOverride.NONE);
        Address ft=ins.getMaxAddress().next();
        if(ins.getFallThrough()==null){ ins.clearFallThroughOverride(); }
        if(ft!=null && L.getInstructionAt(ft)==null && L.getDefinedDataAt(ft)==null) todo.add(ft);
      }
      for(Address a: todo){ if(L.getInstructionAt(a)==null){ new DisassembleCommand(a,null,true).applyTo(currentProgram,monitor); n++; } }
      fixed+=n; if(n==0) break;
    }
    int bodies=0;
    for(Function f: fm.getFunctions(true)){ if(CreateFunctionCmd.fixupFunctionBody(currentProgram,f,monitor)) bodies++; }
    println("FIXNORET cleared="+cleared+" fallthroughs="+fixed+" bodies="+bodies);
  }
}
