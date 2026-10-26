#ifndef TEST_ASSET_TRACER_H_
#define TEST_ASSET_TRACER_H_
typedef struct { int unused; } SM_Tracer;
#define SM_TRACE_BEGIN(id, len) do {
#define SM_TRACE_PAYLOAD_BE(value) ((void)(value));
#define SM_TRACE_END() } while (0);
#endif
