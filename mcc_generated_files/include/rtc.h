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

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "../utils/compiler.h"
#include <stdint.h>
#include <stdbool.h>

#define SCHEDULER_BASE_PERIOD 10    // ms

/** Datatype used to hold the number of ticks until a timer expires */
typedef uint32_t ticks;

/** Typedef for the function pointer for the timeout callback function */
typedef ticks (*task_callback)(void *payload);

/** Data structure completely describing one timer */
typedef struct strTask {
	task_callback   callback;   ///< function that is called when this task is due
    char *          name;       ///< optionally assign a name (for debugging)
	void *          payload;    ///< data to pass along to callback function
	ticks           period;     ///< The task period
	struct strTask  *next;      ///< linked list of all tasks that have expired and whose
	                            ///  functions are due to be called
    ticks           due;        ///< the time when this task is due
} strTask_t;

/**
 * \brief Initialize the driver
 *
 * \return Nothing
 */
void scheduler_init(void);

//********************************************************
// The following functions form the API for scheduler mode.
//********************************************************

/**
 * \brief Schedule the specified timer task to execute at the specified time
 *
 * \param[in] timer Pointer to struct describing the task to execute
 * \param[in] timeout Number of ticks to wait before executing the task
 *
 * \return Nothing
 */
void scheduler_create_task(strTask_t *task, ticks period);

/**
 * \brief Delete the specified timer task so it won't be executed
 *
 * \param[in] timer Pointer to struct describing the task to execute
 *
 * \return Nothing
 */
void scheduler_delete_task(strTask_t *task);

/**
 * \brief Delete all scheduled timer tasks
 *
 * \return Nothing
 */
void scheduler_flush_all(void);

/**
 * \brief Execute the next timer task that has been scheduled for execution.
 *
 * If no task has been scheduled for execution, the function
 * returns immediately, so there is no need for any polling.
 *
 * \return Nothing
 */
void scheduler_next(void);


#endif /* SCHEDULER_H */

/** @}*/