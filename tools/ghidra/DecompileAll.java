// Decompile every recovered function in the program to one text file, in
// address order. Used to produce a full per-chip decode (docs/ghidra/) rather
// than picking functions by hand.
//
// Run via: tools/analyze.sh DecompileAll.java <outfile>
//      or: tools/analyze-8051.sh DecompileAll.java <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import java.io.PrintWriter;

public class DecompileAll extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: DecompileAll.java <outfile>");
            return;
        }
        PrintWriter out = new PrintWriter(args[0]);

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        for (FunctionIterator it = fm.getFunctions(true); it.hasNext();) {
            Function f = it.next();
            if (f.isThunk() || f.isExternal()) continue;
            DecompileUtil.printDecompiled(di, f, monitor, out);
            n++;
        }
        out.println("// function_count=" + n);
        out.close();
        println("wrote " + args[0] + " (" + n + " functions)");
    }
}
