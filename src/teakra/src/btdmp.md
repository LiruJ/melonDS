# BTDMP

## MMIO Layout

```
Receive config registers
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x0280    |   |   |   |   |   |   |RIR|   |   |   |   |   |   |   |   |   |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x0282    |   |   |   |                               |       |           |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x0284    |                            "0004"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x0286    |                            "0021"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x0288    |                            "0000"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x028A    |                            "0000"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x028C    |                            "0000"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x028E    |                               ?                               |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x0290    |                               ?                               |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
...
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x029E    |RE |                                                           |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#

RIR: enable IRQ for receive if 1
RE:  enable receive if 1

Transmit config registers
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02A0    |   |   |   |   |   |   |TIR|   |   |   |   |   |   |   |   |   |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02A2    |   |   |   |                               |       |           | <- clock related, "1004"
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02A4    |                            "0004"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02A6    |                            "0021"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02A8    |                            "0000"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02AA    |                            "0000"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02AC    |                            "0000"?                            |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02AE    |                               ?                               |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02B0    |                               ?                               |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
...
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02BE    |RT |                                                           |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#

TIR: enable IRQ for transmit if 1
TE:  enable transmit if 1

Receive/transmit status and data registers
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02C0    |                                               |RF |           |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02C2    |                                           |TE |TF |           |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02C4    |                         FIFO_RECEIVE                          |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02C6    |                         FIFO_TRANSMIT                         |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02C8    |                               ?                               |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#
|+0x02CA    |                                                   |FL |       |
+-----------#---+---+---+---#---+---+---+---#---+---+---+---#---+---+---+---#

RF: 1 if receive buffer is full
TF: 1 if receive buffer is full
TE: 1 if receive buffer is empty
FL: Application spin waits on this flag

```
