#pragma once

extern int fd_log_events, fd_log_pipes;

static const char * const log_created_pipe_fmt = "Process %d created pipe [r:%d, w:%d]\n";

static const char * const log_closed_fd_fmt = "Process %d closed fd %d\n";

void init_log_fd();
