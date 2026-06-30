// Headless report for the UClean1 MC9S08GT flash.
// Prints the recovered function list and every access to the SPI / IIC
// peripheral registers (one line each, safe for log capture).
//
// Run via: tools/analyze.sh DumpReport.java
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class DumpReport extends GhidraScript {
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        println("=== FUNCTIONS ===");
        int count = 0;
        for (FunctionIterator it = fm.getFunctions(true); it.hasNext();) {
            Function f = it.next();
            println(f.getEntryPoint() + "  " + f.getName() + "  size=" + f.getBody().getNumAddresses());
            count++;
        }
        println("function_count=" + count);

        // MC9S08GB60 direct-page peripheral registers
        long[] offs = {0x28, 0x29, 0x2a, 0x2b, 0x2d, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d};
        String[] names = {"SPI1C1", "SPI1C2", "SPI1BR", "SPI1S", "SPI1D",
                          "IIC1A", "IIC1F", "IIC1C1", "IIC1S", "IIC1D", "IIC1C2"};
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        ReferenceManager rm = currentProgram.getReferenceManager();
        println("=== PERIPHERAL REGISTER ACCESSES ===");
        for (int i = 0; i < offs.length; i++) {
            Address a = sp.getAddress(offs[i]);
            StringBuilder sb = new StringBuilder();
            int n = 0;
            for (ReferenceIterator rit = rm.getReferencesTo(a); rit.hasNext();) {
                Reference r = rit.next();
                sb.append(r.getFromAddress()).append(" ");
                n++;
            }
            println(names[i] + " (0x" + Long.toHexString(offs[i]) + "): " + n + " refs  " + sb);
        }
    }
}
