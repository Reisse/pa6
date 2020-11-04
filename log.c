#include "common.h"
#include "log.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

int fd_log_events, fd_log_pipes;

void init_log_fd()
{
	fd_log_events = open(events_log, O_CREAT | O_TRUNC | O_WRONLY | O_APPEND,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	fd_log_pipes = open(pipes_log, O_CREAT | O_TRUNC | O_WRONLY | O_APPEND,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	if (fd_log_events == -1 || fd_log_pipes == -1) {
		abort();
	}
}
