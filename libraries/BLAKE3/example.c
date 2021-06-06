#include "blake3.h"
#include <stdio.h>
#include <unistd.h>

int main() {
  // Initialize the hasher.
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);

  // Read input bytes from stdin.
  unsigned char buf[65536];
  ssize_t n;
  while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
    blake3_hasher_update(&hasher, buf, n);
  }

  // Finalize the hash. BLAKE3_OUT_LEN is the default output length, 32 bytes.
  uint8_t output[12];
  blake3_hasher_finalize(&hasher, output, 12);

  // Print the hash as hexadecimal.
  for (size_t i = 0; i < 12; i++) {
    printf("%02x", output[i]);
  }
  printf("\n");
  return 0;
}
