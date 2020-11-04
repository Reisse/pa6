#include "ipc.h"

#include "framework.h"

#include <unistd.h>

static int send_i(void * self, local_id dst, const Message * msg) {
	worker_t * worker = self;
	ssize_t rc = write(worker->pipe_matrix[dst][worker->id].write,
		msg, sizeof(MessageHeader) + msg->s_header.s_payload_len);
	return rc == sizeof(MessageHeader) + msg->s_header.s_payload_len ? 0 : -1;
}

int send(void * self, local_id dst, const Message * msg)
{
	((Message * /* well... */) msg)->s_header.s_local_time = forward_lamport_time();
	return send_i(self, dst, msg);
}

int send_multicast(void * self, const Message * msg)
{
	((Message * /* well... */) msg)->s_header.s_local_time = forward_lamport_time();

	worker_t * worker = self;
	for (id_size_t i = 0; i < worker->pipe_matrix_sz; ++i) {
		if (i == worker->id) { continue; }
		int rc = send_i(self, i, msg);
		if (rc) { return rc; }
	}
	return 0;
}

int receive(void * self, local_id from, Message * msg)
{
	worker_t * worker = self;

	MessageHeader header = { 0 };
	ssize_t rc = read(worker->pipe_matrix[worker->id][from].read,
		&header, sizeof(header));
	if (rc != sizeof(header)) { return -1; }
	msg->s_header = header;

	rc = read(worker->pipe_matrix[worker->id][from].read,
		msg->s_payload, header.s_payload_len);
	if (rc != header.s_payload_len) { return -1; }

	sync_lamport_time(msg->s_header.s_local_time);

	return 0;
}

int receive_any(void * self, Message * msg)
{
	worker_t * worker = self;
	do for (id_size_t i = 0; i < worker->pipe_matrix_sz; ++i) {
		if (receive(self, i, msg) == 0) { return 0; }
	} while (!0);
}
