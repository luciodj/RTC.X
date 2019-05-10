# RTC.X
## A new scheduler for the IOT project
This is a modified version of the AVR-IOT WG basic project (generated using MCC AVR-IOT lib v1.1.1) with the following improvements:
- The RTC.c modules is modified to use the PIT (Periodic Interrupt Timer) 
- The resulting scheduler is delivering an accurate and repeatable interrupt interval
- The size of the module is reduced by 1K byte
- Minor refactoring to change the name of each "timer" into "task" (which is more appropriate for a scheduler)
- Tasks are re-launched after each activation (must be killed if termination is desired)
- The repeat period is preserved in the task structure 
- An optional "name" can be included to facilitate debugging


