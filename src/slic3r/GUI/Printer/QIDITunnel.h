#ifndef _QIDI__TUNNEL_H_
#define _QIDI__TUNNEL_H_

#ifdef QIDI_DYNAMIC
#  define QIDI_EXPORT
#  define QIDI_FUNC(x) (*x)
#else
#  ifdef _WIN32
#    ifdef QIDI_EXPORTS
#      define QIDI_EXPORT __declspec(dllexport)
#    else
#      define QIDI_EXPORT __declspec(dllimport)
#    endif // QIDI_EXPORTS
#  else
#    define QIDI_EXPORT
#  endif // __WIN32__
#  define QIDI_FUNC(x) x
#endif // QIDI_DYNAMIC

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif // __cplusplus

#ifdef _WIN32
#ifdef __cplusplus
    typedef wchar_t tchar;
#else
    typedef unsigned short tchar;
#endif
#else
    typedef char tchar;
#endif

typedef void* QIDI_Tunnel;

typedef void (*Logger)(void * context, int level, tchar const* msg);

typedef enum __QIDI_StreamType
{
    VIDE,
    AUDI
} QIDI_StreamType;

typedef enum __QIDI_VideoSubType
{
    AVC1,
    MJPG,
} QIDI_VideoSubType;

typedef enum __QIDI_AudioSubType
{
    MP4A
} QIDI_AudioSubType;

typedef enum __QIDI_FormatType
{
    video_avc_packet,
    video_avc_byte_stream,
    video_jpeg,
    audio_raw,
    audio_adts
} QIDI_FormatType;

typedef struct __QIDI_StreamInfo
{
    QIDI_StreamType type;
    int sub_type;
    union {
        struct
        {
            int width;
            int height;
            int frame_rate;
        } video;
        struct
        {
            int sample_rate;
            int channel_count;
            int sample_size;
        } audio;
    } format;
    int format_type;
    int format_size;
    int max_frame_size;
    unsigned char const * format_buffer;
} QIDI_StreamInfo;

typedef enum __QIDI_SampleFlag
{
    f_sync = 1
} QIDI_SampleFlag;

typedef struct __QIDI_Sample
{
    int itrack;
    int size;
    int flags;
    unsigned char const * buffer;
    unsigned long long decode_time;
} QIDI_Sample;

typedef enum __QIDI_Error
{
    QIDI_success,
    QIDI_stream_end,
    QIDI_would_block,
    QIDI_buffer_limit
} QIDI_Error;

#ifdef QIDI_DYNAMIC
typedef struct __QIDILib {
#endif

QIDI_EXPORT int QIDI_FUNC(QIDI_Create)(QIDI_Tunnel* tunnel, char const* path);

QIDI_EXPORT void QIDI_FUNC(QIDI_SetLogger)(QIDI_Tunnel tunnel, Logger logger, void * context);

QIDI_EXPORT int QIDI_FUNC(QIDI_Open)(QIDI_Tunnel tunnel);

QIDI_EXPORT int QIDI_FUNC(QIDI_StartStream)(QIDI_Tunnel tunnel, bool video);

QIDI_EXPORT int QIDI_FUNC(QIDI_StartStreamEx)(QIDI_Tunnel tunnel, int type);

QIDI_EXPORT int QIDI_FUNC(QIDI_GetStreamCount)(QIDI_Tunnel tunnel);

QIDI_EXPORT int QIDI_FUNC(QIDI_GetStreamInfo)(QIDI_Tunnel tunnel, int index, QIDI_StreamInfo* info);

QIDI_EXPORT unsigned long QIDI_FUNC(QIDI_GetDuration)(QIDI_Tunnel tunnel);

QIDI_EXPORT int QIDI_FUNC(QIDI_Seek)(QIDI_Tunnel tunnel, unsigned long time);

QIDI_EXPORT int QIDI_FUNC(QIDI_ReadSample)(QIDI_Tunnel tunnel, QIDI_Sample* sample);

QIDI_EXPORT int QIDI_FUNC(QIDI_SendMessage)(QIDI_Tunnel tunnel, int ctrl, char const* data, int len);

QIDI_EXPORT int QIDI_FUNC(QIDI_RecvMessage)(QIDI_Tunnel tunnel, int* ctrl, char* data, int* len);

QIDI_EXPORT void QIDI_FUNC(QIDI_Close)(QIDI_Tunnel tunnel);

QIDI_EXPORT void QIDI_FUNC(QIDI_Destroy)(QIDI_Tunnel tunnel);

QIDI_EXPORT int QIDI_FUNC(QIDI_Init)();

QIDI_EXPORT void QIDI_FUNC(QIDI_Deinit)();

QIDI_EXPORT char const* QIDI_FUNC(QIDI_GetLastErrorMsg)();

QIDI_EXPORT void QIDI_FUNC(QIDI_FreeLogMsg)(tchar const* msg);

#ifdef QIDI_DYNAMIC
} QIDILib;
#endif

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _QIDI__TUNNEL_H_
