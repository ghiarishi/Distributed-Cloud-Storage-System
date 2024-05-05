#ifndef HEADER_H
#define HEADER_H

#define BUFFER_SIZE 16384
#define CHECKPOINTING_THRESHOLD 3 // number of writes in checkpointing file after which we update the disk file and flush logs
#define HEARTBEAT_INTERVAL 100 // heartbeat sent at this interval
#define DEAD_THRESHOLD 300 // if no heartbeat for this long, server dead
#define EXPECTED_BYTES_TO_READ_WHEN_CONNECTING_TO_SERVER 60

#endif