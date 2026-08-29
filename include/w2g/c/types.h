#ifndef W2G_C_TYPES_H_
#define W2G_C_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int W2gResult;

#define W2G_SIDE_WASI 0
#define W2G_SIDE_GXX 1

#define W2G_RESULT_OK 0u
#define W2G_RESULT_INVALID_ARGUMENT 3u
#define W2G_RESULT_NOT_FOUND 5u
#define W2G_RESULT_ALREADY_EXISTS 6u
#define W2G_RESULT_UNIMPLEMENTED 12u

#ifdef __cplusplus
}
#endif

#endif  // W2G_C_TYPES_H_
