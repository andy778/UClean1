// Decompile every recovered function in the program to one text file, in
// address order. Used to produce a full per-chip decode (docs/ghidra/) rather
// than picking functions by hand.
//
// Run via: tools/analyze.sh DecompileAll.java <outfile>
//      or: tools/analyze-8051.sh DecompileAll.java <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
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
            out.println("########## " + f.getName() + " @ " + f.getEntryPoint()
                    + "  size=" + f.getBody().getNumAddresses() + " ##########");
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            out.println((res != null && res.getDecompiledFunction() != null)
                    ? res.getDecompiledFunction().getC() : "<decompile failed>");
            out.println();
            n++;
        }
        out.println("// function_count=" + n);
        out.close();
        println("wrote " + args[0] + " (" + n + " functions)");
    }
}
