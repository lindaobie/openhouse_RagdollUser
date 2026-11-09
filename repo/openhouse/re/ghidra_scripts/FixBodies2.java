import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
// Clear and re-disassemble FDE ranges whose function body is < 80% covered.
public class FixBodies2 extends GhidraScript {
  public void run() throws Exception {
    FunctionManager fm=currentProgram.getFunctionManager(); Listing L=currentProgram.getListing();
    long base=currentProgram.getImageBase().getOffset(); int fixed=0, still=0;
    for(String l: Files.readAllLines(Paths.get(getScriptArgs()[0]))){
      String[] p=l.split("\\.\\."); long s=Long.parseUnsignedLong(p[0],16)+base, e=Long.parseUnsignedLong(p[1],16)+base;
      Address a=toAddr(s), end=toAddr(e-1);
      Function f=fm.getFunctionAt(a);
      long covered=0; if(f!=null) for(AddressRange r: f.getBody()) covered+=r.getLength();
      if(f!=null && covered >= (e-s)*8/10) continue;
      String name = f!=null ? f.getName() : null; boolean user = f!=null && f.getSymbol().getSource()==SourceType.USER_DEFINED;
      // remove functions starting inside the range (except at a) and our function
      java.util.List<Function> ov=new java.util.ArrayList<>(); fm.getFunctionsOverlapping(new AddressSet(a,end)).forEachRemaining(ov::add);
      for(Function g: ov) if(!g.getEntryPoint().equals(a) && g.getEntryPoint().compareTo(a)>0) fm.removeFunction(g.getEntryPoint());
      if(f!=null) fm.removeFunction(a);
      L.clearCodeUnits(a,end,false);
      disassemble(a);
      // disassemble any leftover undefined spots (code reached only via jump tables / landing pads)
      for(Address c=a; c.compareTo(end)<=0;){
        if(L.getInstructionAt(c)==null && L.getDefinedDataAt(c)==null){ disassemble(c); }
        Instruction ins=L.getInstructionAt(c); c = ins!=null ? ins.getMaxAddress().next() : c.add(1);
      }
      Function nf=createFunction(a, name!=null && user ? name : null);
      if(nf==null){ still++; continue; }
      CreateFunctionCmd.fixupFunctionBody(currentProgram,nf,monitor);
      fixed++;
    }
    println("FIXBODIES2 fixed="+fixed+" failed="+still);
  }
}
