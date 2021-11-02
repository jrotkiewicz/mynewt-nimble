/*
   wqueue.h
   Worker thread queue based on the Standard C++ library list
   template class.
   ------------------------------------------
   Copyright (c) 2013 Vic Hargrave
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

// https://vichargrave.github.io/articles/2013-01/multithreaded-work-queue-in-cpp
// https://github.com/vichargrave/wqueue/blob/master/wqueue.h


#ifndef _WQUEUE_H_
#define _WQUEUE_H_

#include <pthread.h>
#include "sys/queue.h"
#include "nimble/os_types.h"

//typedef uint32_t ble_npl_time_t;
//typedef void ble_npl_event_fn(struct ble_npl_event *ev);
//
//struct ble_npl_event {
//    uint8_t                 ev_queued;
//    ble_npl_event_fn       *ev_cb;
//    void                   *ev_arg;
//    STAILQ_ENTRY(ble_npl_event) ev_next;
//};
//
//struct ble_npl_eventq {
//    STAILQ_HEAD(, ble_npl_event)	m_queue;
//    pthread_mutex_t      		m_mutex;
//    pthread_mutexattr_t  		m_mutex_attr;
//    pthread_cond_t       		m_condv;
//};

static inline void wqueue_init(struct ble_npl_eventq *evq)
{
	STAILQ_INIT(&evq->m_queue);
	pthread_mutexattr_init(&evq->m_mutex_attr);
	pthread_mutexattr_settype(&evq->m_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&evq->m_mutex, &evq->m_mutex_attr);
	pthread_cond_init(&evq->m_condv, NULL);
}

static inline void wqueue_deinit(struct ble_npl_eventq *evq) {
	pthread_mutex_destroy(&(evq->m_mutex));
	pthread_cond_destroy(&(evq->m_condv));
}

static inline void wqueue_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev) {
	pthread_mutex_lock(&(evq->m_mutex));
	STAILQ_INSERT_TAIL(&evq->m_queue, ev, ev_next);
	pthread_cond_signal(&(evq->m_condv));
	pthread_mutex_unlock(&(evq->m_mutex));
}

static inline int wqueue_size(struct ble_npl_eventq *evq)
{
	struct ble_npl_event *ev;
	int cnt;

	cnt = 0;
	pthread_mutex_lock(&(evq->m_mutex));
	STAILQ_FOREACH(ev, &evq->m_queue, ev_next) {
		cnt++;
	}
	pthread_mutex_unlock(&(evq->m_mutex));

	return cnt;
}

static inline struct ble_npl_event *wqueue_get(struct ble_npl_eventq *evq, uint32_t tmo) {
	pthread_mutex_lock(&(evq->m_mutex));
	if (tmo) {
		while (wqueue_size(evq) == 0) {
	    	pthread_cond_wait(&(evq->m_condv), &(evq->m_mutex));
	    }
	}

	struct ble_npl_event *item = NULL;

	if (wqueue_size(evq) != 0) {
	    item = STAILQ_FIRST(&evq->m_queue);
		STAILQ_REMOVE_HEAD(&evq->m_queue, ev_next);//m_queue.pop_front();
	}

	pthread_mutex_unlock(&(evq->m_mutex));
	return item;
}

static inline void wqueue_remove(struct ble_npl_eventq *evq, struct ble_npl_event * ev) {
	pthread_mutex_lock(&(evq->m_mutex));
	STAILQ_REMOVE(&evq->m_queue, ev, ble_npl_event, ev_next);
	pthread_mutex_unlock(&(evq->m_mutex));
}

#endif
