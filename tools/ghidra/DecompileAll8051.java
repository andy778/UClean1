// Full decode of the nRF9E5's embedded 8051 image: seed disassembly from the
// interrupt vector table + known routines (Nrf9e5Seeds.ADDRS, same seeds as
// Dump8051.java), then follow every CALL/ACALL/LCALL target transitively to
// find the rest of the code (the auto-analyzer finds 0 functions on a raw
// image with no declared entry point), create a function at each call
// target, and decompile all of them to one file.
//
// This only follows CALL-type flows, not jumps/branches within a seed's own
// body, so it can still miss functions reached only via a branch the linear
// scan from an earlier seed doesn't pass through (this happened for two
// functions in this image - see docs/ghidra/nrf9e5_full.c's header comment
// for how those were force-decompiled separately and folded in by hand).
//
// Run via: tools/analyze-8051.sh DecompileAll8051.java <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
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

        Set<Long> funcStarts = new LinkedHashSet<>();
        Deque<Long> work = new ArrayDeque<>();
        for (long s : Nrf9e5Seeds.ADDRS) { work.add(s); }

        Listing listing = currentProgram.getListing();

        while (!work.isEmpty()) {
            long a = work.poll();
            if (!funcStarts.add(a)) continue;
            try { disassemble(toAddr(a)); } catch (Exception e) { continue; }

            // Walk forward from this seed collecting call targets until we hit
            // a RET/RETI or run off the end of disassembled code.
            for (InstructionIterator it = listing.getInstructions(toAddr(a), true); it.hasNext();) {
                Instruction ins = it.next();
                String mn = ins.getMnemonicString();
                if (mn.equals("CALL") || mn.equals("ACALL") || mn.equals("LCALL")) {
                    for (Address f : ins.getFlows()) {
                        long fo = f.getOffset();
                        if (!funcStarts.contains(fo)) work.add(fo);
                    }
                }
                if (mn.equals("RET") || mn.equals("RETI")) break;
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
            DecompileUtil.printDecompiled(di, f, monitor, out);
            n++;
        }
        out.println("// function_count=" + n);
        out.close();
        println("wrote " + args[0] + " (" + n + " functions)");
    }
}
