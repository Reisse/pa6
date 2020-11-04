#pragma once

#include "ipc.h"
#include "queue.h"

#include <stdbool.h>
#include <stdint.h>

typedef local_id id_size_t;

typedef struct {
	int read, write;
} pipe_pair_t;

typedef struct {
	local_id id;
	id_size_t pipe_matrix_sz;
	pipe_pair_t ** pipe_matrix;
	bool mutexcl_enabled;
	queue_s queue;
	id_size_t done_counter;
	id_size_t cs_reply_counter;
	id_size_t started_counter;
} worker_t;

Message init_message(MessageType type, void * payload, size_t payload_len);

timestamp_t get_lamport_time();
timestamp_t forward_lamport_time();
timestamp_t sync_lamport_time(timestamp_t new_lamport_time);

int run_distributed_system(id_size_t workers_count, bool mutexcl);
