# GSD Debug Knowledge Base

Resolved debug sessions. Used by `gsd-debugger` to surface known-pattern hypotheses at the start of new investigations.

---

## ftx1-speaker-24bit-rejected — FTX-1 USB speaker fails to open, only supports 16-bit audio
- **Date:** 2026-07-07
- **Error patterns:** No suitable alt setting found, ESP_ERR_NOT_FOUND, uac_host_device_start, speaker pre-start failed, 24-bit rejected, FTX-1, C-Media codec, bit_resolution
- **Root cause:** The FTX-1's USB audio codec (C-Media-style chip, VID:0x0d8c PID:0x0016) only supports 16-bit sample resolution on its audio streaming interfaces. Mini-FT8's TX path only ever constructed a single fixed 2ch/24-bit/48000Hz stream config with no fallback, so negotiation always failed.
- **Fix:** Added dds_render_16bit_stereo() alongside dds_render_24bit_stereo() in main/dds_q15.h/.cpp; added a candidate-scan loop in stream_uac.cpp's TX_CONNECTED handler (24-bit then 16-bit fallback, FTX-1 only, QMX unchanged); spk_writer_task dispatches renderer and write size based on negotiated bit_resolution.
- **Files changed:** main/dds_q15.h, main/dds_q15.cpp, main/stream_uac.cpp
---

