# Bit Lab

A small Wick 0.3 language example for an upcoming processor-building game.
Run `./build/lantern games/bitlab`. Left/right selects A or B; up/down changes
the byte; Z cycles ADD, AND, XOR. Click/touch an input bit to toggle it; click
the result row to change operation. The flags show the 8080 ADD/ANA/XRA
behaviour: carry, zero, sign, even parity, and auxiliary carry.

This is a register/ALU workbench, not a complete CPU, instruction decoder,
cycle-accurate emulator, circuit editor, or the finished game. PC rollover
is demonstrated separately. The implementation uses flat records, typed
lists, masks, shifts, wrapping and fixed-width labels without host-side CPU logic.

Flag reference: Intel, [8080/8085 Assembly Language Programming (1977)](https://st.sdf-eu.org/i8080/Intel%208080-8085%20Assembly%20Language%20Programming%201977%20Intel.pdf), Auxiliary Carry Flag.
