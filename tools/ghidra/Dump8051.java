// Disassemble the nRF9E5 8051 image and print every instruction.
//
// The ZEROBIT auto-analysis can miss code reached only through the interrupt
// vector table or computed calls, so we seed disassembly from the reset vector,
// the interrupt targets, and the known radio routines (the W_CONFIG writer, the
// RX-payload reader, and the SPI byte primitive) before dumping.
//
// Run via: tools/analyze-8051.sh Dump8051.java
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;

public class Dump8051 extends GhidraScript {
    public void run() throws Exception {
        for (long s : Nrf9e5Seeds.ADDRS) {
            try { disassemble(toAddr(s)); } catch (Exception e) {}
        }
        Listing l = currentProgram.getListing();
        InstructionIterator it = l.getInstructions(true);
        StringBuilder sb = new StringBuilder();
        int n = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            sb.append(ins.getAddressString(false, true))
              .append("  ").append(ins.toString()).append("\n");
            n++;
        }
        println("=== 8051 DISASSEMBLY (" + n + " instrs) ===\n" + sb.toString());
    }
}
