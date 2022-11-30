/* Ensure that the executable of a running process cannot be
   modified. */

#include <syscall.h>
#include <stdio.h>
#include "tests/lib.h"
#include "tests/main.h"

#define TEST_CHECK(SUCCESS, ...)                     \
        do                                      \
          {                                     \
            printf (__VA_ARGS__);               \
            printf("\n");                       \
            if (!(SUCCESS))                     \
              printf ("Fail\n");               \
          }                                     \
        while (0)

void
main (void) 
{
  int handle;
  char buffer[16];
  
  handle = open ("test");
  // printf("File handle: %d\n", handle);
  TEST_CHECK (handle > 1, "open \"test\"");
  TEST_CHECK (read (handle, buffer, sizeof buffer) == (int) sizeof buffer,
         "read \"test\"");
  int result = write (handle, buffer, sizeof buffer);
  printf ("Write value: %d\n", result);
  TEST_CHECK (result == 0,
         "try to write \"test\"");
}
