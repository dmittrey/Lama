#ifndef VERIFY_RUN_H
#define VERIFY_RUN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 on success, 1 on verification error or load failure. */
int run_verify(const char *path);

#ifdef __cplusplus
}
#endif

#endif
