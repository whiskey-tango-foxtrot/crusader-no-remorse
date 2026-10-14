// Dumps function list + decompiled C for the current program.
// @category Crusader
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DumpProgram extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/tmp";
        String name = currentProgram.getName().replace('.', '_');
        println("Program: " + currentProgram.getName() + " lang=" + currentProgram.getLanguageID() + " cspec=" + currentProgram.getCompilerSpec().getCompilerSpecID());
        FunctionManager fm = currentProgram.getFunctionManager();
        int total = fm.getFunctionCount();
        println("Functions: " + total);
        PrintWriter list = new PrintWriter(new FileWriter(outDir + "/" + name + "_functions.txt"));
        int named = 0;
        for (Function f : fm.getFunctions(true)) {
            String n = f.getName();
            if (!n.startsWith("FUN_")) named++;
            list.println(f.getEntryPoint() + "\t" + n + "\t" + f.getBody().getNumAddresses());
        }
        list.close();
        println("Named functions: " + named);
        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(new BufferedWriter(new FileWriter(outDir + "/" + name + "_decomp.c")));
        int i = 0;
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            DecompileResults res = ifc.decompileFunction(f, 60, monitor);
            out.println("// ===== " + f.getEntryPoint() + " " + f.getName() + " =====");
            if (res != null && res.decompileCompleted() && res.getDecompiledFunction() != null) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("// decompile failed: " + (res == null ? "null" : res.getErrorMessage()));
            }
            i++;
            if (i % 200 == 0) println("decompiled " + i + "/" + total);
        }
        out.close();
        ifc.dispose();
        println("Done: " + outDir + "/" + name + "_decomp.c");
    }
}
