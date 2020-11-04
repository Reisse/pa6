#define _GNU_SOURCE // pipe2

#include "framework.h"

#include "log.h"
#include "pa2345.h"

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static timestamp_t lamport_time = 0;

timestamp_t forward_lamport_time()
{
	return ++lamport_time;
}

timestamp_t sync_lamport_time(timestamp_t new_lamport_time)
{
	if (new_lamport_time > lamport_time) {
		lamport_time = new_lamport_time + 1;
	} else {
		++lamport_time;
	}
	return lamport_time;
}

timestamp_t get_lamport_time()
{
	return lamport_time;
}

extern id_size_t last_received_from;

static int handle_message(worker_t * worker)
{
	Message m;
	while (receive_any(worker, &m));

	switch (m.s_header.s_type) {
		case CS_REQUEST:
		{
			id_size_t source = last_received_from;

			if (worker->fork == DirtyFork) {
				worker->fork = AbsentFork;

				m = init_message(CS_RELEASE, NULL, 0);
				send(worker, source, &m);
			} else {
				worker->reqs[source] = true;
			}
		}
			break;

		case CS_RELEASE:
			worker->fork = CleanFork;
			break;

		case DONE:
		{
			// Drop request from that process, if any
			id_size_t source = last_received_from;
			worker->reqs[source] = false;

			++(worker->done_counter);
		}
			break;

		case STARTED:
			++(worker->started_counter);
			break;
	}

	return 0;
}

int request_cs(const void * self)
{
	// Cast away constness...
	worker_t * worker = (worker_t *) self;

	// If we have a dirty fork, and we have not yet cede it to anyone,
	// we can go inside CS again
	if (worker->fork == DirtyFork) {
		worker->fork = CleanFork;
	}

	// If we have a clean fork, go straight in. Though, we can only get a
	// clean fork "for free" if we had a dirty fork before, or if we're
	// entering the CS recursively
	if (worker->fork == CleanFork) {
		return 0;
	}

	// If we don't have a clean fork, we have to request it
	Message m = init_message(CS_REQUEST, NULL, 0);
	send_multicast(worker, &m);

	while (worker->fork != CleanFork) {
		int rc = handle_message(worker);
		if (rc != 0) { return rc; }
	}

	return 0;
}

int release_cs(const void * self)
{
	// Cast away constness...
	worker_t * worker = (worker_t *) self;

	// Sanity check
	assert(worker->fork == CleanFork);

	// Mark our fork as dirty
	worker->fork = DirtyFork;

	// If someone requested fork, pass it
	for (id_size_t i = 0; i < worker->pipe_matrix_sz; ++i) {
		id_size_t rq_id = (worker->id + i) % worker->pipe_matrix_sz;
		if (worker->reqs[rq_id]) {
			// Clean requests as they will be handled by other processes
			memset(worker->reqs, 0, sizeof(bool) * worker->pipe_matrix_sz);

			worker->fork = AbsentFork;

			Message m = init_message(CS_RELEASE, NULL, 0);
			return send(worker, rq_id, &m);
		}
	}
	return 0;
}

/* static void dump_pipe_matrix(int fd, worker_t * worker)
{
	dprintf(fd, "size: %d\n", worker->pipe_matrix_sz);
	for (id_size_t i = 0; i < worker->pipe_matrix_sz; ++i) {
		for (id_size_t j = 0; j < worker->pipe_matrix_sz; ++j) {
			dprintf(fd, "[r:%2d, w:%2d], ", worker->pipe_matrix[i][j].read, worker->pipe_matrix[i][j].write);
		}
		dprintf(fd, "\n");
	}
	dprintf(fd, "\n");
} */

static void open_pipes(worker_t * worker)
{
	for (id_size_t i = 0; i < worker->pipe_matrix_sz; ++i) {
		for (id_size_t j = 0; j < worker->pipe_matrix_sz; ++j) {
			if (i == j) { continue; }

			int pipes[2] = { 0 };
			pipe2(pipes, O_NONBLOCK);

			worker->pipe_matrix[i][j].read = pipes[0];
			worker->pipe_matrix[i][j].write = pipes[1];

			dprintf(fd_log_pipes, log_created_pipe_fmt, /* always called by parent */ 0,
				worker->pipe_matrix[i][j].read, worker->pipe_matrix[i][j].write);
		}
	}
}

/* @mask: 1 - read, 2 - write, 3 - read & write
   @process: used only for logging */
static void close_pipe_and_log(pipe_pair_t * pipes, int mask, local_id process)
{
	if (mask & 1) {
	    close(pipes->read);
	    dprintf(fd_log_pipes, log_closed_fd_fmt, process, pipes->read);
	    pipes->read = -1;
	}

	if (mask & 2) {
		close(pipes->write);
		dprintf(fd_log_pipes, log_closed_fd_fmt, process, pipes->write);
		pipes->write = -1;
	}
}

static void close_unused_pipes(worker_t * worker)
{
	local_id self_id = worker->id;

	for (id_size_t i = 0; i < worker->pipe_matrix_sz; ++i) {
		if (i == self_id) {
			continue;
		}

		close_pipe_and_log(&(worker->pipe_matrix[self_id][i]), 2, self_id);

		for (id_size_t j = 0; j < worker->pipe_matrix_sz; ++j) {
			if (i == j) { continue; }

			close_pipe_and_log(&(worker->pipe_matrix[i][j]), 1, self_id);

			if (j == self_id) { continue; }

			close_pipe_and_log(&(worker->pipe_matrix[i][j]), 2, self_id);
		}
	}
}

static worker_t * alloc_worker(id_size_t pipe_matrix_sz)
{
	worker_t * worker = (worker_t *) malloc(sizeof(worker_t));
	worker->id = -1;

	worker->pipe_matrix_sz = pipe_matrix_sz;
	worker->pipe_matrix = (pipe_pair_t **) malloc(sizeof(pipe_pair_t *) * pipe_matrix_sz);

	for (id_size_t i = 0; i < pipe_matrix_sz; ++i) {
		worker->pipe_matrix[i] = (pipe_pair_t *) malloc(sizeof(pipe_pair_t) * pipe_matrix_sz);

		for (id_size_t j = 0; j < pipe_matrix_sz; ++j) {
			worker->pipe_matrix[i][j].read = worker->pipe_matrix[i][j].write = -1;
		}
	}

	worker->mutexcl_enabled = false;
	worker->done_counter = 0;
	worker->started_counter = 0;

	worker->fork = AbsentFork;
	worker->reqs = (bool *) malloc(sizeof(bool) * pipe_matrix_sz);
	for (id_size_t i = 0; i < pipe_matrix_sz; ++i) {
		worker->reqs[i] = false;
	}

	return worker;
}

/* unused, resources are freed by OS
static void free_worker(worker_t * worker)
{
	for (id_size_t i = 0; i < worker->pipe_matrix_sz; ++i) {
		for (id_size_t j = 0; j < worker->pipe_matrix_sz; ++j) {
			if (worker->pipe_matrix[i][j].read != -1) {
				close_pipe_and_log(&(worker->pipe_matrix[i][j]), 1, worker->id);
			}

			if (worker->pipe_matrix[i][j].write != -1) {
				close_pipe_and_log(&(worker->pipe_matrix[i][j]), 2, worker->id);
			}
		}

		free(worker->pipe_matrix[i]);
	}

	free(worker->pipe_matrix);
	free(worker->reqs);
	free(worker);
}
*/

static int worker_task(worker_t * worker)
{
	// Event: Process 'STARTED'
	dprintf(fd_log_events, log_started_fmt, get_lamport_time(),
		worker->id, getpid(), getppid(), 0);

	char buffer[MAX_PAYLOAD_LEN];
	snprintf(buffer, MAX_PAYLOAD_LEN, log_started_fmt,
		get_lamport_time(), worker->id,
		getpid(), getppid(), 0);
	Message msg = init_message(STARTED, buffer, strlen(buffer));
	send_multicast(worker, &msg);

	while (worker->started_counter != worker->pipe_matrix_sz - 2 /* parent and self */) {
		int rc = handle_message(worker);
		if (rc != 0) { return rc; }
	}

	// Event: Received all 'STARTED'
	dprintf(fd_log_events, log_received_all_started_fmt, get_lamport_time(), worker->id);

	// Work
	id_size_t iter_count = worker->id * 5;
	for (id_size_t i = 0; i < iter_count; ++i) {
		if (worker->mutexcl_enabled) {
			request_cs(worker);
		}

		snprintf(buffer, MAX_PAYLOAD_LEN, log_loop_operation_fmt, worker->id,
			i + 1, iter_count);
		print(buffer);

		if (worker->mutexcl_enabled) {
			release_cs(worker);
		}
	}

	// Event: Work 'DONE'
	dprintf(fd_log_events, log_done_fmt, get_lamport_time(), worker->id, 0);

	snprintf(buffer, MAX_PAYLOAD_LEN, log_done_fmt, get_lamport_time(), worker->id, 0);
	msg = init_message(DONE, buffer, strlen(buffer));
	send_multicast(worker, &msg);

	while (worker->done_counter != worker->pipe_matrix_sz - 2 /* parent and self */) {
		int rc = handle_message(worker);
		if (rc != 0) { return rc; }
	}

	// Event: Received all 'DONE'
	dprintf(fd_log_events, log_received_all_done_fmt, get_lamport_time(), worker->id);

	return 0;
}

static int parent_task(worker_t * worker)
{
	while (worker->started_counter != worker->pipe_matrix_sz - 1 /* self IS parent */
			|| worker->done_counter != worker->pipe_matrix_sz - 1 /* self IS parent */) {
		int rc = handle_message(worker);
		if (rc != 0) { return rc; }
	}

	return 0;
}

Message init_message(MessageType type, void * payload, size_t payload_len)
{
    MessageHeader header;
    header.s_magic = MESSAGE_MAGIC;
    header.s_payload_len = payload_len;
    header.s_type = type;

    Message message;
    message.s_header = header;
    if (payload) {
    	memcpy(&(message.s_payload), payload, payload_len);
    }

    return message;
}

int run_distributed_system(id_size_t workers_count, bool mutexcl)
{
	init_log_fd();	

	worker_t * worker_template = alloc_worker(workers_count + 1 /* +1 for parent */);
	open_pipes(worker_template);
	worker_template->mutexcl_enabled = mutexcl;

	// dump_pipe_matrix(0 /* stdout */, worker_template);

	pid_t children[workers_count];
	for (id_size_t i = 1; i < workers_count + 1 /* id 0 is parent */; ++i) {
		pid_t child = fork();
		if (child > 0) {
			children[i - 1] = child;
		} else if (child == 0) {
			worker_template->id = i;

			// First worker has "dirty" fork in the beginning
			worker_template->fork = (i == 1) ? DirtyFork : AbsentFork;

			close_unused_pipes(worker_template);
			exit(worker_task(worker_template));
		} else {
			abort();
		}
	}

	worker_template->id = 0;
	close_unused_pipes(worker_template);

	int rc = parent_task(worker_template);

	for (id_size_t i = 0; i < workers_count; ++i) {
		int status;
		waitpid(children[i], &status, 0);
	}

	return rc;
}
