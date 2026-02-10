#ifndef STDLIB_H
#define STDLIB_H

#include <cmoc.h>
#include <coco.h>

#define malloc(len) sbrk(len)

#endif /* STDLIB_H */