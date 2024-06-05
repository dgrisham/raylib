/*
This implements a data source that decodes HE-AAC streams via uuac

This object can be plugged into any `ma_data_source_*()` API and can also be used as a custom
decoding backend. See the custom_decoder example.

You need to include this file after miniaudio.h.
*/
#ifndef miniaudio_fdkaac_h
#define miniaudio_fdkaac_h

#include <errno.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(MA_NO_FDKACC)
#include <libavformat/avformat.h>
#include <fdk-aac/aacdecoder_lib.h>
#endif

typedef struct
{
    ma_data_source_base ds;     /* The aac decoder can be used independently as a data source. */
    ma_read_proc onRead;
    ma_seek_proc onSeek;
    ma_tell_proc onTell;
    void* pReadSeekTellUserData;
    ma_format format;           /* Will be either f32 or s16. */
#if !defined(MA_NO_FDKACC)
    HANDLE_AACDECODER handle;
    AVFormatContext *in;
	AVStream *st;

    INT_PCM *decode_buf;
    int decode_buf_size; // total size of the decode buffer
    int decode_buf_start; // index of the first unprocessed pcm value in the decode buffer

    uint64_t pcm_frame_cursor;

    CStreamInfo *info;
#endif
} ma_fdkaac;

MA_API ma_result ma_fdkaac_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_fdkaac* pAAC);
MA_API ma_result ma_fdkaac_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_fdkaac* pAAC);
MA_API void ma_fdkaac_uninit(ma_fdkaac* pAAC, const ma_allocation_callbacks* pAllocationCallbacks);
MA_API ma_result ma_fdkaac_read_pcm_frames(ma_fdkaac* pAAC, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead);
MA_API ma_result ma_fdkaac_seek_to_pcm_frame(ma_fdkaac* pAAC, ma_uint64 frameIndex);
MA_API ma_result ma_fdkaac_get_data_format(ma_fdkaac* pAAC, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap);
MA_API ma_result ma_fdkaac_get_cursor_in_pcm_frames(ma_fdkaac* pAAC, ma_uint64* pCursor);
MA_API ma_result ma_fdkaac_get_length_in_pcm_frames(ma_fdkaac* pAAC, ma_uint64* pLength);

#ifdef __cplusplus
}
#endif
#endif

#if defined(MINIAUDIO_IMPLEMENTATION) || defined(MA_IMPLEMENTATION)

static ma_result ma_fdkaac_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    return ma_fdkaac_read_pcm_frames((ma_fdkaac*)pDataSource, pFramesOut, frameCount, pFramesRead);
	// return MA_NOT_IMPLEMENTED;
}

static ma_result ma_fdkaac_ds_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
	return MA_NOT_IMPLEMENTED;
    // return ma_fdkaac_seek_to_pcm_frame((ma_fdkaac*)pDataSource, frameIndex);
}

static ma_result ma_fdkaac_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    return ma_fdkaac_get_data_format((ma_fdkaac*)pDataSource, pFormat, pChannels, pSampleRate, pChannelMap, channelMapCap);
}

static ma_result ma_fdkaac_ds_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    return ma_fdkaac_get_cursor_in_pcm_frames((ma_fdkaac*)pDataSource, pCursor);
    // *pCursor = 0;
	// return MA_NOT_IMPLEMENTED;
}

static ma_result ma_fdkaac_ds_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
 //    *pLength = 0;
	// return MA_NOT_IMPLEMENTED;
    return ma_fdkaac_get_length_in_pcm_frames((ma_fdkaac*)pDataSource, pLength);
}

static ma_data_source_vtable g_ma_fdkaac_ds_vtable =
{
    ma_fdkaac_ds_read,
    ma_fdkaac_ds_seek,
    ma_fdkaac_ds_get_data_format,
    ma_fdkaac_ds_get_cursor,
    ma_fdkaac_ds_get_length
};

static ma_result ma_fdkaac_init_internal(const ma_decoding_backend_config* pConfig, ma_fdkaac* pAAC)
{
    ma_result result;
    ma_data_source_config dataSourceConfig;

    if (pAAC == NULL) {
        return MA_INVALID_ARGS;
    }

    MA_ZERO_OBJECT(pAAC);
    // pAAC->format = ma_format_f32; /* f32 by default. */
    pAAC->format = ma_format_s16; /* s16 by default. */

    if (pConfig != NULL && (pConfig->preferredFormat == ma_format_f32 || pConfig->preferredFormat == ma_format_s16)) {
        pAAC->format = pConfig->preferredFormat;
    } else {
        /* Getting here means something other than f32 and s16 was specified. Just leave this unset to use the default format. */
    }

    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_fdkaac_ds_vtable;

    result = ma_data_source_init(&dataSourceConfig, &pAAC->ds);
    if (result != MA_SUCCESS) {
        return result;  /* Failed to initialize the base data source. */
    }

    return MA_SUCCESS;
}

MA_API ma_result ma_fdkaac_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_fdkaac* pAAC)
{
    ma_result result;

    (void)pAllocationCallbacks; /* Can't seem to find a way to configure memory allocations in fdkaac. */

    result = ma_fdkaac_init_internal(pConfig, pAAC);
    if (result != MA_SUCCESS) {
        return result;
    }

    if (onRead == NULL || onSeek == NULL) {
        return MA_INVALID_ARGS; /* onRead and onSeek are mandatory. */
    }

    pAAC->onRead = onRead;
    pAAC->onSeek = onSeek;
    pAAC->onTell = onTell;
    pAAC->pReadSeekTellUserData = pReadSeekTellUserData;

    #if !defined(MA_NO_FDKACC)
    {
 		pAAC->handle = aacDecoder_Open(TT_MP4_RAW, 1); // TODO: can these args vary? first one in particular
        return MA_SUCCESS;
    }
    #else
    {
        /* fdkaac is disabled. */
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

// Decodes a single AAC frame and stores the result in pAAC->decode_buf. This always writes from the start of the
// buffer and doesn't care if there's data in it already. After decoding the frame this queries the AAC decoder
// for info on the current stream (number of channels, frame size, etc.) and updates in pAAC->info.
ma_result decode_one_aac_frame(ma_fdkaac* pAAC) {
    ma_result result = MA_SUCCESS;
	AAC_DECODER_ERROR err;

    while (1) {
        UINT valid;
        AVPacket pkt = { 0 };
        int ret = av_read_frame(pAAC->in, &pkt);
            if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                continue;
            }
            result = MA_AT_END;  /* could be another error ? from avformat.h:
                                 *  @return 0 if OK, < 0 on error or end of file. On error, pkt will be blank
                                 *          (as if it came from av_packet_alloc()).
                                 */
            break;
        }
        if (pkt.stream_index != pAAC->st->index) {
        	av_packet_unref(&pkt);
        	continue;
        }

        valid = pkt.size;
        UINT input_length = pkt.size;

        err = aacDecoder_Fill(pAAC->handle, &pkt.data, &input_length, &valid);
        if (err != AAC_DEC_OK) {
            fprintf(stderr, "Fill failed: %x\n", err);
            result = MA_ERROR;
            break;
        }

        err = aacDecoder_DecodeFrame(pAAC->handle, pAAC->decode_buf, pAAC->decode_buf_size / sizeof(INT_PCM), 0);
        pAAC->decode_buf_start = 0;

        av_packet_unref(&pkt);
        if (err == AAC_DEC_NOT_ENOUGH_BITS) {
        	continue;
        }
        if (err != AAC_DEC_OK) {
        	fprintf(stderr, "Decode failed: %x\n", err);
        	result = MA_ERROR;
        	break;
        }

        if (!pAAC->info) {
        	pAAC->info = aacDecoder_GetStreamInfo(pAAC->handle);
        	if (!pAAC->info || pAAC->info->sampleRate <= 0) {
        		fprintf(stderr, "No stream info\n");
        		result = MA_ERROR;
        	}
        }

        break;
    }
    return result;
}

MA_API ma_result ma_fdkaac_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_fdkaac* pAAC)
{
    ma_result result;

    // (void)pAllocationCallbacks; // TODO ?

    result = ma_fdkaac_init_internal(pConfig, pAAC);
    if (result != MA_SUCCESS) {
        return result;
    }


    #if !defined(MA_NO_FDKACC)
    {
    	int ret;
    	unsigned i;
    	AVFormatContext *in = NULL;
    	AVStream *st = NULL;
    	UINT input_length;
    	AAC_DECODER_ERROR err;

    #if LIBAVFORMAT_VERSION_MICRO < 100 || LIBAVFORMAT_VERSION_MAJOR < 58 || LIBAVFORMAT_VERSION_MINOR < 9
        av_register_all();
    #endif
        ret = avformat_open_input(&in, pFilePath, NULL, NULL);
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            fprintf(stderr, "%s: %s\n", pFilePath, buf);
            return MA_INVALID_FILE;
        }
        for (i = 0; i < in->nb_streams && !st; i++) {
            if (in->streams[i]->codecpar->codec_id == AV_CODEC_ID_AAC) {
                st = in->streams[i];
                break;
            }
        }
        if (!st) {
            fprintf(stderr, "No AAC stream found\n");
            return MA_INVALID_DATA;
        }
        if (!st->codecpar->extradata_size) {
            fprintf(stderr, "No AAC ASC found\n");
            return MA_INVALID_DATA;
        }

        if (pAAC->handle == NULL) {
     		pAAC->handle = aacDecoder_Open(TT_MP4_RAW, 1); // TODO: can these args vary? first one in particular
        }
        pAAC->pcm_frame_cursor = 0;
        pAAC->in = in;
        pAAC->st = st;
    	input_length = st->codecpar->extradata_size;
        err = aacDecoder_ConfigRaw(pAAC->handle, &st->codecpar->extradata, &input_length);
        if (err != AAC_DEC_OK) {
            fprintf(stderr, "Unable to decode the ASC\n");
            return MA_INVALID_DATA;
        }
        pAAC->info = NULL;

    	pAAC->decode_buf_size = 8*2048*sizeof(INT_PCM); // larger than we probably need (maybe 2048 * 2 * sizeof(INT_PCM) is more realistic?)
    													// HE-AAC maxes at 2048 PCM frames in one AAC frame * 2 channels (maybe the example
    													// used 8 for 7.1 surround?)
        pAAC->decode_buf = (INT_PCM*)ma_malloc(pAAC->decode_buf_size, pAllocationCallbacks);
        pAAC->decode_buf_start = -1; // -1 means we don't currently have any valid data in the buffer

        // loads one frame into the buffer and initializes pAAC->info (so we have number of channels / etc.)
		return decode_one_aac_frame(pAAC);
    }
    #else
    {
        /* fdkaac is disabled. */
        (void)pFilePath;
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API void ma_fdkaac_uninit(ma_fdkaac* pAAC, const ma_allocation_callbacks* pAllocationCallbacks)
{
    #if !defined(MA_NO_FDKACC)
    {
        if (pAAC == NULL) {
            return;
        }
        if (pAAC->decode_buf) ma_free(pAAC->decode_buf, pAllocationCallbacks);
        if (pAAC->in)         avformat_close_input(&pAAC->in);
        if (pAAC->handle)     aacDecoder_Close(pAAC->handle);
    }
    #else
    {
        /* fdkaac is disabled. Should never hit this since initialization would have failed. */
        MA_ASSERT(MA_FALSE);
    }
    #endif

    ma_data_source_uninit(&pAAC->ds);
}

MA_API ma_result ma_fdkaac_read_pcm_frames(ma_fdkaac* pAAC, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    if (pFramesRead != NULL) {
        *pFramesRead = 0;
    }

    if (frameCount == 0) {
        return MA_INVALID_ARGS;
    }

    if (pAAC == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_FDKACC)
    {
        ma_result result = MA_SUCCESS;  /* Must be initialized to MA_SUCCESS. */
        ma_uint64 totalPCMFramesRead = 0;

    	INT_PCM* pcmOut = pFramesOut;

        while (1) {
            // these values can technically change on each DecodeFrame call (but shouldn't?) since pAAC->info is updated
            ma_uint32 numChannels = pAAC->info ? pAAC->info->numChannels : 2;
            ma_uint32 frameSize = pAAC->info ? pAAC->info->frameSize : 1024; // 1024 is common for AAC-LC
            int decode_buf_end = numChannels * frameSize; // index of the last valid decoded value in the decode buffer.
                                                          // this should never be > the total buffer size (since we fill/drain one frame at a time, and the buffer is big)

			for (int i = pAAC->decode_buf_start; i < decode_buf_end; i += numChannels) {
    			for (unsigned j = 0; j < numChannels; ++j)
            		*(pcmOut++) = pAAC->decode_buf[i + j];

				++totalPCMFramesRead;
    			pAAC->decode_buf_start += numChannels;

				if (totalPCMFramesRead == frameCount) goto DONE;
    		}

    		// buffer not full, decode another frame and continue
			result = decode_one_aac_frame(pAAC);
			if (result != MA_SUCCESS) {
    			break;
			}
        }
DONE:
        if (pFramesRead != NULL) {
            *pFramesRead = totalPCMFramesRead;
            pAAC->pcm_frame_cursor += totalPCMFramesRead; // TODO: is this off-by-one? not sure what exactly miniaudio expects this to mean
        }
        if (result == MA_SUCCESS && totalPCMFramesRead == 0) {
            result = MA_AT_END;
        }
        return result;
    }
    #else
    {
        /* fdkaac is disabled. Should never hit this since initialization would have failed. */
        MA_ASSERT(MA_FALSE);

        (void)pFramesOut;
        (void)frameCount;
        (void)pFramesRead;

        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_fdkaac_seek_to_pcm_frame(ma_fdkaac* pAAC, ma_uint64 frameIndex)
{
    if (pAAC == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_FDKACC)
    {
        // TODO: this may not be implemented correctly in the library itself (was a custom addition)
        // aacDecoder_SetBlockNumber(pAAC->handle, frameIndex);
        return MA_SUCCESS;
    }
    #else
    {
        /* fdkaac is disabled. Should never hit this since initialization would have failed. */
        MA_ASSERT(MA_FALSE);

        (void)frameIndex;

        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_fdkaac_get_data_format(ma_fdkaac* pAAC, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    /* Defaults for safety. */
    if (pFormat != NULL) {
        *pFormat = ma_format_unknown;
    }
    if (pChannels != NULL) {
        *pChannels = 0;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = 0;
    }
    if (pChannelMap != NULL) {
        MA_ZERO_MEMORY(pChannelMap, sizeof(*pChannelMap) * channelMapCap);
    }

    if (pAAC == NULL) {
        return MA_INVALID_OPERATION;
    }

    if (pFormat != NULL) {
        *pFormat = pAAC->format;
    }

    #if !defined(MA_NO_FDKACC)
    {
        if (pChannels != NULL) {
            if (pAAC->info) *pChannels = pAAC->info->numChannels;
        }

        if (pSampleRate != NULL) {
            if (pAAC->info) *pSampleRate = pAAC->info->sampleRate;
        }

        if (pChannelMap != NULL) {
            ma_channel_map_init_standard(ma_standard_channel_map_fdkaac, pChannelMap, channelMapCap, *pChannels);
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* fdkaac is disabled. Should never hit this since initialization would have failed. */
        MA_ASSERT(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}


MA_API ma_result ma_fdkaac_get_cursor_in_pcm_frames(ma_fdkaac* pAAC, ma_uint64* pCursor)
{
    if (pCursor == NULL) {
        return MA_INVALID_ARGS;
    }

    *pCursor = 0;   /* Safety. */

    if (pAAC == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_FDKACC)
    {
        // INT offset = aacDecoder_GetBlockNumber(pAAC->handle);
        *pCursor = (ma_uint64)pAAC->pcm_frame_cursor;
        return MA_SUCCESS;
    }
    #else
    {
        /* fdkaac is disabled. Should never hit this since initialization would have failed. */
        MA_ASSERT(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_fdkaac_get_length_in_pcm_frames(ma_fdkaac* pAAC, ma_uint64* pLength)
{
    if (pLength == NULL) {
        return MA_INVALID_ARGS;
    }

    *pLength = 0;   /* Safety. */

    if (!pAAC || !pAAC->st) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_FDKACC)
    {
        // this is assuming nb_frames is the number of AAC frames -- which it seems to be, based on the sizes I'm seeing (9261 frames for a 3.5 min song)
        *pLength = (ma_uint64)pAAC->st->nb_frames * pAAC->info->frameSize;
        return MA_SUCCESS;
    }
    #else
    {
        /* fdkaac is disabled. Should never hit this since initialization would have failed. */
        MA_ASSERT(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

#endif
