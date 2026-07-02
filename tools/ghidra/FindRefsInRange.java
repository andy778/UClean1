// List every instruction that references a data address in [lo, hi], grouped by
// the function it lives in. Useful for finding the consumers of a RAM buffer or
// a parameter table (who reads the SPI receive buffer, who reads a setpoint).
//
// Run via: tools/analyze.sh FindRefsInRange.java <outfile> <lohex> <hihex>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;

public class FindRefsInRange extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("usage: FindRefsInRange.java <outfile> <lohex> <hihex>");
            return;
        }
        PrintWriter out = new PrintWriter(args[0]);
        long lo = Long.parseLong(args[1].replaceFirst("^0x", ""), 16);
        long hi = Long.parseLong(args[2].replaceFirst("^0x", ""), 16);
        out.printf("refs into [0x%x, 0x%x]%n", lo, hi);

        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            for (Reference r : ins.getReferencesFrom()) {
                Address to = r.getToAddress();
                if (to == null || !to.isMemoryAddress()) continue;
                long o = to.getOffset();
                if (o < lo || o > hi) continue;
                Function f = getFunctionContaining(ins.getAddress());
                out.printf("  0x%-6s %-6s -> 0x%-4x  [%s] %s%n",
                        ins.getAddress().toString(),
                        r.getReferenceType().getName(),
                        o,
                        f != null ? f.getName() : "?",
                        ins.toString());
            }
        }
        out.close();
        println("wrote " + args[0]);
    }
}
