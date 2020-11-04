#include "queue.h"

#include <assert.h>
#include <stdbool.h>

local_id q_top(queue_s * queue)
{
	assert(queue->length > 0);

	queue_entry_s top_entry = { MAX_PRIO, MAX_PROCESS_ID };

	for (size_t i = 0; i < queue->length; ++i) {
		if (queue->items[i].prio < top_entry.prio) {
			top_entry = queue->items[i];
		} else if (queue->items[i].prio == top_entry.prio) {
			if (queue->items[i].id < top_entry.id) {
				top_entry = queue->items[i];
			}
		}
	}

	return top_entry.id;
}

void q_rm(queue_s * queue, local_id id)
{
	assert(queue->length > 0);

	bool move = false;
	--(queue->length);
	for (size_t i = 0; i < queue->length; ++i) {
		if (queue->items[i].id == id) {
			move = true;
		}

		if (move) {
			queue->items[i] = queue->items[i+1];
		}
	}
}

void q_add(queue_s * queue, local_id id, timestamp_t prio)
{
	for (size_t i = 0; i < queue->length; ++i) {
		assert(queue->items[i].id != id);
	}

	queue->items[queue->length++] = (queue_entry_s) { prio, id };
}
