// Disassemble every byte in [lo, hi] as instructions and print them in order,
// regardless of Ghidra's auto-detected function boundaries. Useful for reading
// straight-line code in a gap the auto-analyzer never wrapped in a Function
// (so DecompileCallers/ForceDecompile have no correct entry point to anchor on).
//
// Run via: tools/analyze.sh DisassembleRange.java <outfile> <lohex> <hihex>
//   or:     tools/analyze-8051.sh DisassembleRange.java <outfile> <lohex> <hihex>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import java.io.PrintWriter;

public class DisassembleRange extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("usage: DisassembleRange.java <outfile> <lohex> <hihex>");
            return;
        }
        PrintWriter out = new PrintWriter(args[0]);
        long lo = Long.parseLong(args[1].replaceFirst("^0x", ""), 16);
        long hi = Long.parseLong(args[2].replaceFirst("^0x", ""), 16);

        Address a = toAddr(lo);
        Address end = toAddr(hi);
        int n = 0;
        while (a.compareTo(end) <= 0) {
            Instruction ins = getInstructionAt(a);
            if (ins == null) {
                try {
                    disassemble(a);
                    ins = getInstructionAt(a);
                } catch (Exception e) { /* fall through */ }
            }
            if (ins == null) {
                out.printf("%s  ??  db 0x%02x%n", a, getByte(a) & 0xff);
                a = a.add(1);
                continue;
            }
            out.printf("%s  %s%n", a, ins.toString());
            n++;
            a = a.add(ins.getLength());
        }
        out.close();
        println("wrote " + args[0] + " with " + n + " instructions");
    }
}
