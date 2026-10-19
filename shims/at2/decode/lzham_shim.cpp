// Minimal C shims for the LZHAM decompress API (decompress-only build).
#include "lzham_decomp.h"

extern "C" lzham_decompress_state_ptr LZHAM_CDECL lzham_decompress_init(const lzham_decompress_params *pParams) {
    return lzham::lzham_lib_decompress_init(pParams);
}
extern "C" lzham_decompress_state_ptr LZHAM_CDECL lzham_decompress_reinit(lzham_decompress_state_ptr pState, const lzham_decompress_params *pParams) {
    return lzham::lzham_lib_decompress_reinit(pState, pParams);
}
extern "C" lzham_uint32 LZHAM_CDECL lzham_decompress_deinit(lzham_decompress_state_ptr pState) {
    return lzham::lzham_lib_decompress_deinit(pState);
}
extern "C" lzham_decompress_status_t LZHAM_CDECL lzham_decompress(
    lzham_decompress_state_ptr pState, const lzham_uint8 *pIn_buf, size_t *pIn_buf_size,
    lzham_uint8 *pOut_buf, size_t *pOut_buf_size, lzham_bool no_more_input_bytes_flag) {
    return lzham::lzham_lib_decompress(pState, pIn_buf, pIn_buf_size, pOut_buf, pOut_buf_size, no_more_input_bytes_flag);
}
extern "C" lzham_decompress_status_t LZHAM_CDECL lzham_decompress_memory(
    const lzham_decompress_params *pParams, lzham_uint8 *pDst_buf, size_t *pDst_len,
    const lzham_uint8 *pSrc_buf, size_t src_len, lzham_uint32 *pAdler32) {
    return lzham::lzham_lib_decompress_memory(pParams, pDst_buf, pDst_len, pSrc_buf, src_len, pAdler32);
}
extern "C" lzham_uint32 LZHAM_CDECL lzham_get_version(void) {
    return 0x1010;
}
extern "C" void LZHAM_CDECL lzham_set_memory_callbacks(lzham_realloc_func pRealloc, lzham_msize_func pMSize, void *pUser_data) {
    lzham::lzham_lib_set_memory_callbacks(pRealloc, pMSize, pUser_data);
}
