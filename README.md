# operating-systems-pa-1

## CFR report_doc

## Code Description

The purpose of protecting regions of code with disable_interrupts is to avoid the situation in which 
an interrupt disrupts a system call, or an interrupt disrupts another interrupt handler. 

## Tests: 
To test the behavior of the system under different loads, we created a file called **test_stress.c**. 
The purpose of this file is to see what happens when the execution times of the processes is similar
to the delay between the network interrupts. What we found was that when the network interrupts were 
infrequent, and the execution time of the process was quick, the idle process would run for several
clock intervals. This is reasonable behavior, because while the blocked processes haven't finished,
the system needs to wait. 

When the network interrupts happen frequently, and the processes run long, the idle process is
not invoked as often. It still runs when the first thread blocks before creating any new threads,
but after that there are enough threads in the ready queue to keep the CPU busy between network
interrupts. 

## Conclusions

The main problems we had were 
