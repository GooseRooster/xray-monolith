#include "stdafx.h"

#include "GIFStream.h"

#include "gif_lib.h"



static int Read_GIF_Fn(GifFileType* handle, GifByteType* buffer, int bytesToRead)
{
    CStreamReader* const reader = static_cast<CStreamReader*>(handle->UserData);
    if (reader->eof()) return 0;

    const u32 pos = reader->tell();
    reader->r(buffer, static_cast<u32>(bytesToRead));
    return static_cast<int>(reader->tell() - pos);
}

static const char* ToString_GifRecordType(GifRecordType type)
{
#define _CASE(T) case GifRecordType::T: return #T
    switch (type)
    {
        _CASE(SCREEN_DESC_RECORD_TYPE);
        _CASE(IMAGE_DESC_RECORD_TYPE);
        _CASE(EXTENSION_RECORD_TYPE);
        _CASE(TERMINATE_RECORD_TYPE);
        default:
            return "UNDEFINED_RECORD_TYPE";
    }
#undef _CASE
}



CGIFStream::CGIFStream()
    : m_Fname(),
      m_Handle(nullptr),
      m_Reader(nullptr),
      m_TempImage(nullptr),
      m_TempLine(nullptr),
      m_DelayMS(0),
      m_TimeMS(0),
      m_ReadStart(0),
      m_ImageSize(0),
      m_TransparentIdx(0xFF),
      m_IsDamaged(false)
{
    //
}

CGIFStream::~CGIFStream()
{
    if (m_Handle)
    {
        DGifCloseFile(m_Handle, nullptr);
        FS.r_close(m_Reader);
        Memory.mem_free(m_TempLine);
        Memory.mem_free(m_TempImage);
    }
}

void CGIFStream::reset_reader()
{
    m_Reader->seek(static_cast<int>(m_ReadStart));
}

void CGIFStream::decode_next()
{
    GifRecordType record_type = GifRecordType::UNDEFINED_RECORD_TYPE;
    bool done = false;
    do
    {
        if (DGifGetRecordType(m_Handle, &record_type) != GIF_OK)
        {
            set_corruption("DGifGetRecordType");
            break;
        }

        switch (record_type)
        {
            case GifRecordType::EXTENSION_RECORD_TYPE:
            {
                int code = 0;
                GifByteType* ext = nullptr;
                if (DGifGetExtension(m_Handle, &code, &ext) != GIF_OK)
                {
                    set_corruption("DGifGetExtension");
                    done = true;
                    break;
                }

                if (code == GRAPHICS_EXT_FUNC_CODE && ext)
                {
                    m_DelayMS = (ext[2] | (ext[3] << 8)) * 10;
                    m_TransparentIdx = (ext[1] & 0x01) ? ext[4] : 0xFF;
                }

                while (ext)
                {
                    if (DGifGetExtensionNext(m_Handle, &ext) != GIF_OK)
                    {
                        set_corruption("DGifGetExtensionNext");
                        done = true;
                        break;
                    }
                }
                break;
            }
            case GifRecordType::IMAGE_DESC_RECORD_TYPE:
            {
                if (DGifGetImageDesc(m_Handle) != GIF_OK)
                {
                    set_corruption("DGifGetImageDesc");
                    done = true;
                    break;
                }

                const GifWord left = m_Handle->Image.Left;
                const GifWord top = m_Handle->Image.Top;
                const GifWord width = m_Handle->Image.Width;
                const GifWord height = m_Handle->Image.Height;
                ColorMapObject* const colorMap = m_Handle->Image.ColorMap ? m_Handle->Image.ColorMap : m_Handle->SColorMap;
                if (colorMap)
                {
                    VERIFY(width <= m_Handle->SWidth);

                    for (GifWord y = 0; y < height; ++y)
                    {
                        if (DGifGetLine(m_Handle, m_TempLine, width) != GIF_OK)
                        {
                            set_corruption("DGifGetLine");
                            break;
                        }

                        const GifWord canY = top + y;
                        for (GifWord x = 0; x < width; ++x)
                        {
                            const GifWord canX = left + x;
                            const u8 idx = m_TempLine[x];
                            if (idx != m_TransparentIdx && idx < colorMap->ColorCount)
                            {
                                const u32 offset = static_cast<u32>((canY * m_Handle->SWidth + canX) * 4);
                                const GifColorType c = colorMap->Colors[idx];
#if (defined(STATIC_RENDERER_R1) || defined(STATIC_RENDERER_R2))
                                m_TempImage[offset + 0] = c.Blue;
                                m_TempImage[offset + 1] = c.Green;
                                m_TempImage[offset + 2] = c.Red;
                                m_TempImage[offset + 3] = 0xFF;
#else
                                m_TempImage[offset + 0] = c.Red;
                                m_TempImage[offset + 1] = c.Green;
                                m_TempImage[offset + 2] = c.Blue;
                                m_TempImage[offset + 3] = 0xFF;
#endif
                            }
                        }
                    }
                }

                done = true;
                break;
            }
            case GifRecordType::TERMINATE_RECORD_TYPE:
            {
                reset_reader();
                break;
            }
            default:
            {
                Msg("! [ERROR]: Unexpected GIF record [%s] in '%s'", ToString_GifRecordType(record_type), m_Fname.c_str());
                set_corruption(nullptr);
                done = true;
                break;
            }
        }
    } while (!done);
}

void CGIFStream::set_corruption(const char* funcName)
{
    if (!funcName) funcName = "GIF";

    const char* errDesc = GifErrorString(m_Handle->Error);
    if (!errDesc) errDesc = "<no-desc>";

    Msg("! [ERROR]: %s - %s (0x%X) | '%s'", funcName, errDesc, static_cast<u32>(m_Handle->Error), m_Fname.c_str());

    m_IsDamaged = true;
    m_DelayMS = 0xFFFFFFFFU;
}

bool CGIFStream::IsValid() const
{
    return static_cast<bool>(m_Handle);
}

bool CGIFStream::Load(const char* fname, u32 startTime)
{
    VERIFY(m_Handle == nullptr);

    CStreamReader* reader = FS.rs_open(nullptr, fname);
    VERIFY(reader);

    int err = GIF_OK;
    m_Handle = DGifOpen(reader, &Read_GIF_Fn, &err);
    if (!m_Handle)
    {
        const char* errStr = GifErrorString(err);
        if (!errStr) errStr = "<no-desc>";

        Msg("! Can't load gif '%s' (%s)", fname, errStr);
        FS.r_close(reader);
        return false;
    }

    const u32 w = static_cast<u32>(m_Handle->SWidth);
    const u32 h = static_cast<u32>(m_Handle->SHeight);

    m_ImageSize = w * h * 4;
    m_TempLine = static_cast<u8*>(Memory.mem_alloc(w));
    m_TempImage = static_cast<u8*>(Memory.mem_alloc(m_ImageSize));
    m_Reader = reader;
    m_TimeMS = startTime;
    m_DelayMS = 0;
    m_Fname = fname;
    m_ReadStart = reader->tell();
    return true;
}

bool CGIFStream::Update(u32 currentTime)
{
    VERIFY(IsValid());

    // Stop updating the staging buffer if the gif file is corrupted.
    if (m_IsDamaged) return false;

    const u32 diff = currentTime - m_TimeMS;
    if (diff >= m_DelayMS)
    {
        decode_next();
        m_TimeMS = currentTime;
        return !m_IsDamaged;
    }
    return false;
}

void CGIFStream::Reset(u32 currentTime)
{
    VERIFY(IsValid());

    if (m_IsDamaged) return;

    reset_reader();
    m_TimeMS = currentTime;
    m_DelayMS = 0;
}

u32 CGIFStream::Width() const
{
    VERIFY(IsValid());
    return static_cast<u32>(m_Handle->SWidth);
}

u32 CGIFStream::Height() const
{
    VERIFY(IsValid());
    return static_cast<u32>(m_Handle->SHeight);
}

u32 CGIFStream::MemUsage() const
{
    VERIFY(IsValid());
    return m_ImageSize + static_cast<u32>(m_Handle->SWidth);
}

const u8* CGIFStream::ImageData() const
{
    VERIFY(IsValid());
    return m_TempImage;
}

u32 CGIFStream::ImageSize() const
{
    VERIFY(IsValid());
    return m_ImageSize;
}
