// Dump raw bytes from flash at each given address range.
// Run via: tools/analyze.sh DumpBytes.java <outfile> <hexaddr:len> [<hexaddr:len>...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.io.PrintWriter;

public class DumpBytes extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: DumpBytes.java <outfile> <hex:len>..."); return; }
        PrintWriter out = new PrintWriter(args[0]);
        for (int i = 1; i < args.length; i++) {
            String[] p = args[i].split(":");
            long off = Long.parseLong(p[0].replaceFirst("^0x", ""), 16);
            int len = Integer.parseInt(p[1]);
            Address a = toAddr(off);
            StringBuilder hex = new StringBuilder();
            StringBuilder asc = new StringBuilder();
            for (int k = 0; k < len; k++) {
                int b;
                try { b = getByte(a.add(k)) & 0xff; }
                catch (Exception e) { hex.append("?? "); asc.append('.'); continue; }
                hex.append(String.format("%02x ", b));
                asc.append(b >= 0x20 && b < 0x7f ? (char) b : '.');
            }
            out.printf("0x%04x (%d bytes): %s |%s|%n", off, len, hex.toString().trim(), asc);
        }
        out.close();
        println("wrote " + args[0]);
    }
}
