import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class ApplyNames extends GhidraScript {
  public void run() throws Exception {
    int n=0;
    for(String l: Files.readAllLines(Paths.get(getScriptArgs()[0]))){
      if(l.startsWith("#")||l.isBlank()) continue;
      String[] p=l.split("\t"); Function f=getFunctionAt(toAddr(Long.parseLong(p[0],16)));
      if(f!=null){ f.setName(p[1],SourceType.USER_DEFINED); n++; } else println("NOFN "+p[0]);
    }
    println("APPLYNAMES "+n);
  }
}
