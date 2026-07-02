// Force-disassemble and create a function at each given address (even inside a
// code gap the auto-analyzer skipped), then decompile it to a file. Also dumps
// the HCS08 interrupt/reset vector table (0xFFC0-0xFFFF) so ISR entries can be
// matched to their vectors.
//
// Run via: tools/analyze.sh ForceDecompile.java <outfile> <hexaddr> [<hexaddr>...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import java.io.PrintWriter;

public class ForceDecompile extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: ForceDecompile.java <outfile> <hexaddr> [<hexaddr>...]");
            return;
        }
        PrintWriter out = new PrintWriter(args[0]);

        // Dump vector table: 16-bit big-endian pointers at 0xFFC0..0xFFFE.
        out.println("########## VECTOR TABLE 0xFFC0-0xFFFE ##########");
        for (long a = 0xFFC0L; a <= 0xFFFEL; a += 2) {
            Address va = toAddr(a);
            try {
                int hi = getByte(va) & 0xff;
                int lo = getByte(va.add(1)) & 0xff;
                out.printf("  vec @0x%04x -> 0x%04x%n", a, (hi << 8) | lo);
            } catch (Exception e) {
                out.printf("  vec @0x%04x -> <unreadable>%n", a);
            }
        }
        out.println();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        for (int i = 1; i < args.length; i++) {
            long off = Long.parseLong(args[i].replaceFirst("^0x", ""), 16);
            Address addr = toAddr(off);
            Function f = getFunctionContaining(addr);
            if (f == null) {
                disassemble(addr);
                f = createFunction(addr, "FORCED_" + Long.toHexString(off));
            }
            out.println("########## 0x" + Long.toHexString(off) + " -> "
                    + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "<no func>")
                    + " ##########");
            if (f != null) {
                DecompileResults res = di.decompileFunction(f, 60, monitor);
                out.println((res != null && res.getDecompiledFunction() != null)
                        ? res.getDecompiledFunction().getC() : "<decompile failed>");
            }
            out.println();
        }
        out.close();
        println("wrote " + args[0]);
    }
}
