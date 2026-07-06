// Full decode of the nRF9E5's embedded 8051 image: seed disassembly from the
// interrupt vector table + known routines (same seeds as Dump8051.java), then
// follow every CALL/ACALL/LCALL target transitively to find the rest of the
// code (the auto-analyzer finds 0 functions on a raw image with no declared
// entry point), create a function at each call target, and decompile all of
// them to one file.
//
// Run via: tools/analyze-8051.sh DecompileAll8051.java <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.LinkedHashSet;
import java.util.Set;

public class DecompileAll8051 extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: DecompileAll8051.java <outfile>");
            return;
        }

        long[] seeds = {
            0x0000, 0x002b, 0x00cb, 0x0256, 0x029f, 0x0156, 0x01c8, 0x02fb,
        };

        Set<Long> funcStarts = new LinkedHashSet<>();
        Deque<Long> work = new ArrayDeque<>();
        for (long s : seeds) { work.add(s); }

        Listing listing = currentProgram.getListing();
        Set<Long> disassembled = new LinkedHashSet<>();

        while (!work.isEmpty()) {
            long a = work.poll();
            if (!disassembled.add(a)) continue;
            try { disassemble(toAddr(a)); } catch (Exception e) { continue; }
            funcStarts.add(a);

            // Walk forward from this seed collecting call targets until we hit
            // a RET/RETI or fall off into already-visited territory.
            Address cur = toAddr(a);
            for (int i = 0; i < 4000; i++) {
                Instruction ins = listing.getInstructionAt(cur);
                if (ins == null) break;
                String mn = ins.getMnemonicString();
                if (mn.equals("CALL") || mn.equals("ACALL") || mn.equals("LCALL")) {
                    Address[] flows = ins.getFlows();
                    for (Address f : flows) {
                        long fo = f.getOffset();
                        if (!disassembled.contains(fo)) work.add(fo);
                    }
                }
                if (mn.equals("RET") || mn.equals("RETI")) break;
                Address next = ins.getMaxAddress().add(1);
                if (next.equals(cur)) break;
                cur = next;
            }
        }

        for (Long a : funcStarts) {
            try { createFunction(toAddr(a), "F_" + Long.toHexString(a)); } catch (Exception e) {}
        }

        PrintWriter out = new PrintWriter(args[0]);
        out.println("########## VECTOR TABLE (8051 interrupt vectors, 0x0000-0x002B) ##########");
        out.println("  0x0000 reset / 0x0003 ext_int0 / 0x000b timer0 / 0x0013 ext_int1 / 0x001b timer1 / 0x0023 serial");
        out.println();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int n = 0;
        for (Long a : funcStarts) {
            Function f = getFunctionAt(toAddr(a));
            if (f == null) continue;
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
