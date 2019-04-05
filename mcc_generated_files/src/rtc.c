/*
    (c) 2018 Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip software and any
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party
    license terms applicable to your use of third party software (including open source software) that
    may accompany Microchip software.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
    FOR A PARTICULAR PURPOSE.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
    SOFTWARE.
*/

#include <stdio.h>
#include "../include/rtc.h"
#include "../utils/atomic.h"

#define RTC_INT_DISABLE()   do{RTC.INTCTRL &= ~RTC_OVF_bm;}while(0);
#define RTC_INT_ENABLE()    do{RTC.INTCTRL |= RTC_OVF_bm;} while(0);
#define RTC_INT_GET()       ((RTC.INTCTRL & RTC_OVF_bm) >> RTC_OVF_bp)
static strTask_t *tasks_head        = NULL;
static strTask_t *volatile due_head = NULL;

volatile ticks  curr_time = 0;
volatile bool   run       = false;

// compare two timestamps and return true if a >= thenb
// timestamps are unsigned, using Z math (Z = 16-bit or 32-bit)
// so their difference must be < half Z period
// to simplify the check we compute the difference and cast it to signed
inline bool greaterOrEqual(ticks a, ticks thenb)
{
    return ((int16_t)(curr_time - tasks_head->due) >= 0);
}

void scheduler_init(void)
{

	while (RTC.STATUS > 0)
        ;      /* Wait for all register to be synchronized */


	RTC.CTRLA = RTC_PRESCALER_DIV1_gc   /* Prescaling Factor: RTC Clock/1 */
              | 1 << RTC_RTCEN_bp       /* Enabled */
              | 0 << RTC_RUNSTDBY_bp;   /* Run In Standby: disabled */

	RTC.CLKSEL = RTC_CLKSEL_INT1K_gc;   /* Clock Select: Internal 1kHz OSC */

    RTC.INTCTRL = 0 << RTC_CMP_bp       /* Compare Match Interrupt disabled */
                | 1 << RTC_OVF_bp;      /* Overflow Interrupt enabled */

    RTC.CNT = -SCHEDULER_BASE_PERIOD;
}

// Disable all the timers without deleting them from any list. Timers can be
//    restarted by calling startTimerAtHead
void scheduler_stop(void)
{
	RTC_INT_DISABLE();         // disable rtc interrupts
	run = false;
}

inline void rtc_set_period(ticks period)
{ //
	ticks last_load = RTC.CNT - period;     // TODO consider - compensation
	while (RTC.STATUS & RTC_CNTBUSY_bm);    // Wait for clock domain synchronization
	RTC.CNT = last_load;                    //
}

void scheduler_print_list(void)
{
	strTask_t *pTask = tasks_head;
	while (pTask != NULL) {
		printf("%s:%ld -> ", pTask->name, (uint32_t)pTask->period);
		pTask = pTask->next;
	}
	printf(".\n");
}

// Returns true if the insert was at the head, false if not
bool task_queue_insert(strTask_t *task)
{
	uint8_t   at_head       = true;
	strTask_t *insert_point = tasks_head;
	strTask_t *prev_point   = NULL;

    task->next = NULL;

	while (insert_point != NULL) {
		if (greaterOrEqual(insert_point->period, task->due)) {
			break; // found the spot
		}
		prev_point   = insert_point;
		insert_point = insert_point->next;
		at_head      = false;
	}

	if (at_head) { // the front of the list.
		task->next = tasks_head;
		tasks_head = task;
		return true;
	}
    else { // middle of the list
		task->next = prev_point->next;
	}
	prev_point->next = task;
	return false;
}

void check_scheduler_queue(void)
{
	RTC_INT_DISABLE();         // disable rtc interrupts

	if (tasks_head == NULL)     // no tasks left
	{
		scheduler_stop();
		return;
	}

    RTC_INT_ENABLE();
	run = true;
}

// Cancel and remove all active tasks
void scheduler_flush_all(void)
{
	scheduler_stop();
	while (tasks_head != NULL) {
		scheduler_delete_task(tasks_head);
	}

	while (due_head != NULL) {
		scheduler_delete_task(due_head);
	}
}

// Deletes a task from a list and returns true if the task was found and
//     removed from the list specified
bool scheduler_delete(strTask_t *volatile *queue, strTask_t *task)
{
	bool ret_val = false;
	if (*queue == NULL)  {       // the list is empty
		return ret_val;
    }

	RTC_INT_DISABLE();          // disable rtc interrupts

	if (task == *queue) {        // the head is the one we are deleting
		*queue = (*queue)->next;  // Delete the head
		ret_val = true;
		check_scheduler_queue();   // check if the queue is now empty
	}
    else {                      // compare from the second task (if present) down
		strTask_t *delete_point = (*queue)->next;
		strTask_t *prev_task = *queue;   // start from the second element
		while (delete_point != NULL) {
			if (delete_point == task) {
				prev_task->next = delete_point->next; // delete it from list
				ret_val = true;
				break;
			}
			prev_task = delete_point; // advance down the list
			delete_point = delete_point->next;
		}
		RTC_INT_ENABLE();
	}

	return ret_val;
}

// This will cancel/remove a running task. If the task is already due it will
//     also remove it from the callback queue
void scheduler_delete_task(strTask_t *task)
{
    if (!scheduler_delete(&tasks_head, task))
    {
	    scheduler_delete(&due_head, task);
    }

    task->next = NULL;
}


// This function checks the list of due tasks and calls the first one in the
//    list if the list is not empty. It also reschedules the task if on repeat
// It is recommended this is called from the main superloop (while(1)) in your code
//    but by design this can also be called from the task ISR. If you wish callbacks
//    to happen from the ISR context you can call this as the last action in timeout_isr
//    instead.
void scheduler_next(void)
{
	if (due_head == NULL)
		return;

	bool tempIE = RTC_INT_GET();    // save interrupt status
	RTC_INT_DISABLE();              // disable rtc interrupts

	strTask_t *pTask = due_head;    // pick the first task due
	due_head = due_head->next;      // and remove it from the list
    task_queue_insert(pTask);       // re-enter it immediately in the task queue

	if (tempIE) {
        RTC_INT_ENABLE();
    }

	bool reschedule = pTask->callback(pTask->payload); // execute the task

	// did the task decide to terminate (return 0 / false)
	if (!reschedule) {
        scheduler_delete_task(pTask);
	}
}

// This function queues a task with a given period/duration
// If the task was already active/running it will be replaced by this and the
//    old (active) task will be removed/cancelled first
// inputs:
//   ms         time expressed in ms, stored in multiples of SCHEDULER_BASE_PERIOD
//   return     true if successful, false if period < SCHEDULER_BASE_PERIOD
bool scheduler_create_task(strTask_t *task, uint32_t ms)
{
    // If this task is already active, replace it
	scheduler_delete_task(task);

	RTC_INT_DISABLE();         // disable rtc interrupts

    ms /= SCHEDULER_BASE_PERIOD;
    if ((ms == 0) || (ms > MAX_BASE_PERIOD))
        return false;

    task->period = (ticks)ms;               // store period scaled
    task->due = curr_time + task->period;   // compute due time

	// We only have to start the task at head if the insert was at the head
	if (task_queue_insert(task)) {
		check_scheduler_queue();
	}
    else {
		if (run) {
            RTC_INT_ENABLE();
        }
	}
    return true;    // successful inser
}

ISR(RTC_CNT_vect)
{
	rtc_set_period(SCHEDULER_BASE_PERIOD);
    curr_time++;    // forever advancing and wrapping around

    // activate tasks that are due (move to due list))
    while (  (tasks_head)
          && greaterOrEqual(curr_time, tasks_head->due) ) {
            strTask_t * pTask = tasks_head;
            pTask->due += pTask->period;    // update immediately the due time

            pTask->next = due_head;         // insert at head of due
            due_head = pTask;

            tasks_head = tasks_head->next;  // remove task from scheduler queue
        }

	check_scheduler_queue();

	RTC.INTFLAGS = RTC_OVF_bm;  // clear the flag
}
