/* Header to expose a small wrapper for forwarding streaming buffers
 * from the MAX32664C driver into the PPG wrist decoder.
 */
#ifndef HPI_PPG_WRIST_H
#define HPI_PPG_WRIST_H

#include <stdint.h>

/* Forward a mempool-backed buffer (owned by RTIO) to the PPG wrist
 * decoding path. The implementation lives in `smf_ppg_wrist.c` and will
 * call the existing decode routine. This wrapper allows the driver to
 * hand the buffer directly to the decoder when convenient.
 */
void hpi_ppg_wrist_handle_stream_buffer(uint8_t *buf, uint32_t buf_len);

#endif /* HPI_PPG_WRIST_H */
