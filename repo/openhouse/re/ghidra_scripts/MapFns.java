import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.io.*;
import java.util.*;
public class MapFns extends GhidraScript {
  Memory mem;
  String cstr(Address a){ try{ StringBuilder sb=new StringBuilder(); for(int i=0;i<400;i++){ byte b=mem.getByte(a.add(i)); if(b==0)break; if(b<32||b>126) return null; sb.append((char)b);} return sb.length()>=3?sb.toString():null;}catch(Exception e){return null;} }
  public void run() throws Exception {
    String out=getScriptArgs()[0]; mem=currentProgram.getMemory();
    Listing L=currentProgram.getListing(); ReferenceManager rm=currentProgram.getReferenceManager();
    FunctionManager fm=currentProgram.getFunctionManager();
    PrintWriter w=new PrintWriter(new FileWriter(out+"/fn_strings.tsv"));
    PrintWriter c=new PrintWriter(new FileWriter(out+"/fn_calls.tsv"));
    for(Function f: fm.getFunctions(true)){
      Set<String> ss=new LinkedHashSet<>(); Set<String> cs=new LinkedHashSet<>();
      for(Instruction ins: L.getInstructions(f.getBody(),true)){
        for(Reference r: ins.getReferencesFrom()){
          Address t=r.getToAddress();
          if(r.getReferenceType().isCall()){ Function g=fm.getFunctionAt(t); if(g!=null){ if(g.isThunk()) g=g.getThunkedFunction(true); cs.add(g.getEntryPoint()+":"+g.getName()); } }
          else { String s=cstr(t); if(s!=null) ss.add(s.replace("\t"," ").replace("\n","\\n")); }
        }
      }
      w.println(f.getEntryPoint()+"\t"+f.getBody().getNumAddresses()+"\t"+String.join(" | ",ss));
      c.println(f.getEntryPoint()+"\t"+String.join(" ",cs));
    }
    w.close(); c.close();
    // RTTI: find typeinfo name strings "N6vortex..." etc, typeinfo objects point to them at +8
    PrintWriter v=new PrintWriter(new FileWriter(out+"/vtables.tsv"));
    AddressSetView all=mem.getLoadedAndInitializedAddressSet();
    MemoryBlock rel=mem.getBlock(".data.rel.ro");
    Map<Long,String> nameAt=new HashMap<>();
    // scan data.rel.ro qwords for pointers to strings that look like mangled type names
    Map<Long,Long> tiToName=new HashMap<>();
    for(Address a=rel.getStart(); a.compareTo(rel.getEnd())<0; a=a.add(8)){
      long p=mem.getLong(a); Address pa; try{pa=toAddr(p);}catch(Exception e){continue;}
      if(!all.contains(pa)) continue;
      String s=cstr(pa); if(s==null) continue;
      if(s.matches("^(N|[0-9]+[A-Za-z_]).*") && !s.contains(" ")) { tiToName.put(a.getOffset()-8, p); nameAt.put(a.getOffset()-8,s);}
    }
    for(Address a=rel.getStart(); a.compareTo(rel.getEnd())<0; a=a.add(8)){
      long p=mem.getLong(a);
      if(!nameAt.containsKey(p)) continue;
      long ott=mem.getLong(a.subtract(8));
      if(ott>0 || ott< -4096) continue; // offset-to-top must be 0 or small negative
      StringBuilder sb=new StringBuilder();
      Address q=a.add(8);
      for(int i=0;i<200;i++){ long fp=mem.getLong(q.add(i*8)); Function g=null; try{ g=fm.getFunctionAt(toAddr(fp)); }catch(Exception e){} 
        if(g==null){ if(fp==0){sb.append(" [").append(i).append("]0"); continue;} break; }
        sb.append(" [").append(i).append("]").append(g.getEntryPoint()); }
      v.println(a.subtract(8)+"\t"+ott+"\t"+nameAt.get(p)+"\t"+sb);
    }
    v.close();
  }
}
