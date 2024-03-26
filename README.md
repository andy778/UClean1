<!--
SPDX-License-Identifier: MIT
-->
<!--
[![REUSE status](https://api.reuse.software/badge/git.fsfe.org/reuse/api)](https://api.reuse.software/info/git.fsfe.org/reuse/api)
-->

# Uponor Clean 1
Investigate if its possible to get Uponor Clean 1 into Home Assistant

## Hypothesis
* From the modem port connect an ESPHOME and insert values into Home Assistant
* It's 868.35MHz radio between inner unit and outer unit 

![PCB with text](uclean1.png)

## Datasheets 
| No       | Description | IC           |
| ---      | ---         |---           |
| U2       | CPU         | [MC9S08 CTSY1442 411LY 6T32ACFB ](https://www.nxp.com/products/processors-and-microcontrollers/additional-mpu-mcus-architectures/8-bit-s08-mcus/8-bit-general-purpose-gtmcus:S08GT)  |
| U10      | Serial      | [sipex 3232](https://www.silicon-ark.co.uk/datasheets/sp3222_3232e-datasheet-sipex.pdf)   |
| U3       | I2C Mem     | [4128BWP 8424K](https://www.st.com/en/memories/m24128-bw.html) |
| OC13     | Optoisolator| MOC3063 1512 |
| D11      | Mosfet      | VNE46 AC 4DLMG |
| OC6      | Mosfet      | 1435 814 |

