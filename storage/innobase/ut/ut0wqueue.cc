/*****************************************************************************

Copyright (c) 2006, 2011, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is also distributed with certain software (including
but not limited to OpenSSL) that is licensed under separate terms,
as designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have included with MySQL.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

#include "ut0wqueue.h"

/*******************************************************************//**
@file ut/ut0wqueue.cc
A work queue

Created 4/26/2006 Osku Salerma
************************************************************************/

/****************************************************************//**
Create a new work queue.
@return	work queue */
UNIV_INTERN
ib_wqueue_t*
ib_wqueue_create(void)
/*===================*/
{
	ib_wqueue_t*	wq = static_cast<ib_wqueue_t*>(mem_alloc(sizeof(*wq)));

	/* Function ib_wqueue_create() has not been used anywhere,
	not necessary to instrument this mutex */
	mutex_create(PFS_NOT_INSTRUMENTED, &wq->mutex, SYNC_WORK_QUEUE);

	wq->items = ib_list_create();
	wq->event = os_event_create();
	wq->count = 0;

	return(wq);
}

/****************************************************************//**
Free a work queue. */
UNIV_INTERN
void
ib_wqueue_free(
/*===========*/
	ib_wqueue_t*	wq)	/*!< in: work queue */
{
	mutex_free(&wq->mutex);
	ib_list_free(wq->items);
	os_event_free(wq->event);

	mem_free(wq);
}

/****************************************************************//**
Add a work item to the queue. */
UNIV_INTERN
void
ib_wqueue_add(
/*==========*/
	ib_wqueue_t*	wq,	/*!< in: work queue */
	void*		item,	/*!< in: work item */
	mem_heap_t*	heap)	/*!< in: memory heap to use for allocating the
				list node */
{
	mutex_enter(&wq->mutex);

	ib_list_add_last(wq->items, item, heap);
	wq->count++;
	os_event_set(wq->event);

	mutex_exit(&wq->mutex);
}

/****************************************************************//**
Wait for a work item to appear in the queue.
@return	work item */
UNIV_INTERN
void*
ib_wqueue_wait(
/*===========*/
	ib_wqueue_t*	wq)	/*!< in: work queue */
{
	ib_list_node_t*	node;

	for (;;) {
		os_event_wait(wq->event);

		mutex_enter(&wq->mutex);

		node = ib_list_get_first(wq->items);

		if (node) {
			ib_list_remove(wq->items, node);
			wq->count--;
			if (!ib_list_get_first(wq->items)) {
				/* We must reset the event when the list
				gets emptied. */
				os_event_reset(wq->event);
			}

			break;
		}

		mutex_exit(&wq->mutex);
	}

	mutex_exit(&wq->mutex);

	return(node->data);
}

/********************************************************************
read total number of work item to the queue.
@return total count of work item in the queue */
uint64_t
ib_wqueue_get_count(
/*==========*/
	ib_wqueue_t *wq)		/*!< in: work queue */
{
	uint64_t count;
	mutex_enter(&wq->mutex);
	count = wq->count;
	mutex_exit(&wq->mutex);
	return count;
}

/********************************************************************
Wait for a work item to appear in the queue for specified time. */

void*
ib_wqueue_timedwait(
/*================*/
					/* out: work item or NULL on timeout*/
	ib_wqueue_t*	wq,		/* in: work queue */
	ib_time_t	wait_in_usecs)	/* in: wait time in micro seconds */
{
	ib_list_node_t*	node = NULL;

	for (;;) {
		ulint		error;
		ib_int64_t	sig_count;

		mutex_enter(&wq->mutex);

		node = ib_list_get_first(wq->items);

		if (node) {
			ib_list_remove(wq->items, node);
			wq->count--;
			mutex_exit(&wq->mutex);
			break;
		}

		sig_count = os_event_reset(wq->event);

		mutex_exit(&wq->mutex);

		error = os_event_wait_time_low(wq->event,
					       (ulint) wait_in_usecs,
					       sig_count);

		if (error == OS_SYNC_TIME_EXCEEDED) {
			break;
		}
	}

	return(node ? node->data : NULL);
}

/********************************************************************
Check if queue is empty. */

ibool
ib_wqueue_is_empty(
/*===============*/
					/* out: TRUE if queue empty
					else FALSE */
	const ib_wqueue_t*	wq)	/* in: work queue */
{
	return(ib_list_is_empty(wq->items));
}
