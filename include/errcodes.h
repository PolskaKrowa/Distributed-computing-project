#ifndef ERRCODES_H
#define ERRCODES_H

typedef enum {
    OK = 0,
    ERR_INVALID_ARGUMENT = 1,
    ERR_BUFFER_TOO_SMALL = 2,
    ERR_COMPUTATION_FAILED = 3,
    ERR_TIMEOUT = 4,
    ERR_NOT_IMPLEMENTED = 5,   
} errcode_t;

#endif // ERRCODES_H