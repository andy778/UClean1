// Shared "decompile one function, print a ########## header + its C, or a
// failure marker" block, used by DecompileAll.java and DecompileAll8051.java
// so the two full-decode scripts don't each carry their own copy.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.util.task.TaskMonitor;
import java.io.PrintWriter;

public final class DecompileUtil {
    public static void printDecompiled(DecompInterface di, Function f, TaskMonitor monitor, PrintWriter out) {
        out.println("########## " + f.getName() + " @ " + f.getEntryPoint()
                + "  size=" + f.getBody().getNumAddresses() + " ##########");
        DecompileResults res = di.decompileFunction(f, 60, monitor);
        out.println((res != null && res.getDecompiledFunction() != null)
                ? res.getDecompiledFunction().getC() : "<decompile failed>");
        out.println();
    }

    private DecompileUtil() {}
}
