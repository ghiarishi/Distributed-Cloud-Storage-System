#ifndef HEADER_H
#define HEADER_H


// Number of writes in checkpointing file after which we update the disk file and flush logs
#define CHECKPOINTING_THRESHOLD 3
#define HEARTBEAT_INTERVAL 1
#define DEAD_THRESHOLD 3
#define EXPECTED_BYTES_TO_READ_WHEN_CONNECTING_TO_SERVER 48

#endif