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
#include "../mcc.h"
#include "../utils/atomic.h"

#define SCHEDULER_BASE_PERIOD 8    // ms

#define RTC_INT_DISABLE()   do{RTC_PITINTCTRL &= ~RTC_PI_bm;}while(0);
#define RTC_INT_ENABLE()    do{RTC_PITINTCTRL |= RTC_PI_bm;}while(0);
#define RTC_INT_GET()       ((RTC_PITINTCTRL  &  RTC_PI_bm) >> RTC_PI_bp)
#define RTC_INT_CLEAR()     do{RTC_PITINTFLAGS = 1;}while(0);

static strTask_t *tasks_head        = NULL;
static strTask_t *volatile due_head = NULL;

volatile ticks  curr_time = 0;
//volatile bool   run       = false;

// compare two timestamps and return true if a >= thenb
// timestamps are unsigned, using Z math (Z = 16-bit or 32-bit)
// so their difference must be < half Z period
// to simplify the check we compute the difference and cast it to signed
inline bool greaterOrEqual(ticks a, ticks thenb)
{
    return ((int16_t)(a - thenb) >= 0);
}

void scheduler_init(void)
{

	while (RTC.STATUS > 0)
        ;      /* Wait for all register to be synchronized */


	RTC.CTRLA = RTC_PRESCALER_DIV1_gc   /* Prescaling Factor: RTC Clock/1 */
              | 0 << RTC_RTCEN_bp       /* Enabled */
              | 0 << RTC_RUNSTDBY_bp;   /* Run In Standby: disabled */

	RTC.CLKSEL = RTC_CLKSEL_INT1K_gc;   /* Clock Select: Internal 1kHz OSC */

    RTC.PITCTRLA = RTC_PI_bm            // enable PIT function
                 | RTC_PERIOD_CYC8_gc;// 128 (32768/256) cycles per second
    while(RTC.PITSTATUS & RTC_CTRLBUSY_bm);
    RTC_INT_ENABLE();
//	run = true;

}

// Disable all the timers without deleting them from any list. Timers can be
//    restarted by calling startTimerAtHead
//void scheduler_stop(void)
//{
//	RTC_INT_DISABLE();         // disable rtc interrupts
////	run = false;
//}

void scheduler_print_list(void)
{
	strTask_t *pTask = tasks_head;

    printf("@%d tasks_head -> ", curr_time);
	while (pTask != NULL) {
		printf("%s:%ld -> ", pTask->name, (uint32_t)pTask->due);
		pTask = pTask->next;
	}
	printf("NULL\n");
}

// Returns true if the insert was at the head, false if not
void tasks_queue_insert(strTask_t *task)
{
	uint8_t   at_head       = true;
	strTask_t *insert_point = tasks_head;
	strTask_t *prev_point   = NULL;

    task->next = NULL;
	while (insert_point != NULL) {
		if (greaterOrEqual(insert_point->due, task->due)) {
			break; // found the spot
		}
		prev_point   = insert_point;
		insert_point = insert_point->next;
		at_head      = false;
	}

	if (at_head) { // the front of the list.
		task->next = tasks_head;
		tasks_head = task;
		return;
	}
    else { // middle of the list
		task->next = prev_point->next;
	}
	prev_point->next = task;
	return;
}

//void check_scheduler_queue(void)
//{
//	RTC_INT_DISABLE();         // disable rtc interrupts
//
//	if (tasks_head == NULL) {  // no tasks left
////        puts("stop");
//		scheduler_stop();
//		return;
//	}
////    puts("run");
//    RTC_INT_ENABLE();
//	run = true;
//}

// Cancel and remove all active tasks
void scheduler_flush_all(void)
{
//	scheduler_stop();
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
//		check_scheduler_queue();   // check if the queue is now empty
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
	}
    RTC_INT_ENABLE();

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

//	bool tempIE = RTC_INT_GET();    // save interrupt status
	RTC_INT_DISABLE();              // disable rtc interrupts

	strTask_t *pTask = due_head;    // pick the first task due
//    printf("@%d task:%s!\n", curr_time, pTask->name);
	due_head = due_head->next;      // and remove it from the list
    tasks_queue_insert(pTask);       // re-enter it immediately in the task queue
//    scheduler_print_list();
//    if (tempIE) {
        RTC_INT_ENABLE();
//    }

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
bool scheduler_create_task(strTask_t *task, uint16_t ms)
{
    // If this task is already active, replace it
	scheduler_delete_task(task);

    if ((ms == 0) || (ms > MAX_BASE_PERIOD)){
        return false;
    }
	RTC_INT_DISABLE();         // disable rtc interrupts

    task->period = (ticks)ms;               // store period scaled
    task->due = curr_time + task->period;   // compute due time
    tasks_queue_insert(task);
    RTC_INT_ENABLE();
    return true;    // successful creation
}

ISR(RTC_PIT_vect)
{
    curr_time += SCHEDULER_BASE_PERIOD;    // forever advancing and wrapping around
    // activate tasks that are due (move to due list))
    while( (tasks_head)  &&
            greaterOrEqual(curr_time, tasks_head->due) ) {
        tasks_head->due += tasks_head->period;    // update immediately the due time
        strTask_t * pNext = tasks_head->next;     // save next temporarily
        tasks_head->next = due_head;         // insert at head of due
        due_head = tasks_head;
        tasks_head = pNext;             // remove task from scheduler queue
        }

	RTC_INT_CLEAR();
}
