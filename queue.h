#pragma once

#include "ipc.h"

enum {
	MAX_QUEUE_SIZE = 1024,
	MAX_PRIO = 32767
};

typedef struct {
	timestamp_t prio;
	local_id id;
} queue_entry_s;

// _Priority_ queue
typedef struct {
	size_t length;
	queue_entry_s items[MAX_QUEUE_SIZE];
} queue_s;

local_id q_top(queue_s * queue);
void q_add(queue_s * queue, local_id id, timestamp_t prio);
void q_rm(queue_s * queue, local_id id);
