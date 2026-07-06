// Shared disassembly seeds for the nRF9E5's embedded 8051 image
// (dumps/u1-nrf9e5-8051.bin). The auto-analyzer finds 0 functions on this raw
// image (no declared entry point), so both Dump8051.java and
// DecompileAll8051.java need to seed disassembly from the same known
// addresses. Kept in one place so the two scripts can't drift out of sync.
public final class Nrf9e5Seeds {
    public static final long[] ADDRS = {
        0x0000, // reset vector (LJMP 0x2b)
        0x002b, // reset target
        0x00cb, // init
        0x0256, // timer0 interrupt
        0x029f, // serial/SPI interrupt
        0x0156, // nRF905 W_CONFIG writer
        0x01c8, // RX-payload handler (R_RX_PAYLOAD 0x24, 32-byte loop)
        0x02fb, // SPI byte transfer (SPI_DATA 0xb2)
    };

    private Nrf9e5Seeds() {}
}
