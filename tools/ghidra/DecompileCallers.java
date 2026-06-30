// Decompile the functions containing the given addresses, plus their direct
// callers, and write the C to a file. Useful for following a driver primitive
// outward to the code that supplies its arguments.
//
// Run via: tools/analyze.sh DecompileCallers.java <outfile> <hexaddr> [<hexaddr>...]
//   e.g.   tools/analyze.sh DecompileCallers.java /tmp/eeprom.txt a554 ded1
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class DecompileCallers extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DecompileCallers.java <outfile> <hexaddr> [<hexaddr>...]");
            return;
        }
        PrintWriter out = new PrintWriter(args[0]);

        Set<Function> targets = new LinkedHashSet<Function>();
        for (int i = 1; i < args.length; i++) {
            long off = Long.parseLong(args[i].replaceFirst("^0x", ""), 16);
            Function f = getFunctionContaining(toAddr(off));
            if (f == null) {
                out.println("(no function at 0x" + Long.toHexString(off) + ")");
                continue;
            }
            targets.add(f);
            for (ReferenceIterator it = currentProgram.getReferenceManager()
                    .getReferencesTo(f.getEntryPoint()); it.hasNext();) {
                Reference r = it.next();
                Function c = getFunctionContaining(r.getFromAddress());
                if (c != null) targets.add(c);
            }
        }

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (Function f : targets) {
            out.println("########## " + f.getName() + " @ " + f.getEntryPoint() + " ##########");
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            out.println((res != null && res.getDecompiledFunction() != null)
                    ? res.getDecompiledFunction().getC() : "<decompile failed>");
            out.println();
        }
        out.close();
        println("wrote " + args[0] + " with " + targets.size() + " functions");
    }
}
