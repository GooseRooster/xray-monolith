#ifndef _GIF_STREAM_
#define _GIF_STREAM_
#pragma once

#include "../xrCore/Stream_Reader.h"



struct GifFileType;

class CGIFStream : private xray::noncopyable
{
private:
    xr_string m_Fname;
    GifFileType* m_Handle;
    CStreamReader* m_Reader;
    u8* m_TempImage;
    u8* m_TempLine;
    u32 m_DelayMS;
    u32 m_TimeMS;
    u32 m_ReadStart;
    u32 m_ImageSize;
    u8 m_TransparentIdx;
    bool m_IsDamaged;

private:
    void reset_reader();
    void decode_next();
    void set_corruption(const char* funcName);

public:
    CGIFStream();
    ~CGIFStream();

public:
    bool IsValid() const;
    bool Load(const char* fname, u32 startTime);
    bool Update(u32 currentTime);
    void Reset(u32 currentTime);
    u32 Width() const;
    u32 Height() const;
    u32 MemUsage() const;
    const u8* ImageData() const;
    u32 ImageSize() const;
};

#endif
