import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import java.nio.file.*;
import java.util.*;
public class EhFns extends GhidraScript {
  public void run() throws Exception {
    List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
    int made=0, fail=0;
    for (String l : lines) {
      long s = Long.parseUnsignedLong(l.split("\\.\\.")[0],16) + currentProgram.getImageBase().getOffset();
      Address a = toAddr(s);
      if (getFunctionAt(a)!=null) continue;
      new DisassembleCommand(a,null,true).applyTo(currentProgram, monitor);
      if (createFunction(a,null)!=null) made++; else fail++;
    }
    println("EHFNS made="+made+" fail="+fail+" total="+currentProgram.getFunctionManager().getFunctionCount());
  }
}
