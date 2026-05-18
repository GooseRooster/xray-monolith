#include "stdafx.h"
#include "../xrCore/Stream_Reader.h"

#include "GIFResource.h"

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

static const char* get_gif_error_string(GifFileType* gif)
{
    const char* errDesc = GifErrorString(gif->Error);
    return errDesc ? errDesc : "<no-desc>";
}

static bool decode_gif(GifFileType* gif, u8* tempImage, u8* tempLine,
                            xr_vector<CGIFResource::Image>& out,
                            u32 imageSize,
                            const char* fname)
{
    GifRecordType record_type = GifRecordType::UNDEFINED_RECORD_TYPE;
    bool done = false;
    bool result = false;
    u32 lastDelay = 0;
    u8 lastTransparentIdx = 0xFF;
    do
    {
        if (DGifGetRecordType(gif, &record_type) != GIF_OK)
        {
            Msg("! [ERROR]: Failed to read GIF record (%s) in '%s'", get_gif_error_string(gif), fname);
            break;
        }

        switch (record_type)
        {
            case GifRecordType::EXTENSION_RECORD_TYPE:
            {
                int code = 0;
                GifByteType* ext = nullptr;
                if (DGifGetExtension(gif, &code, &ext) != GIF_OK)
                {
                    Msg("! [ERROR]: Failed to read GIF extension (%s) in '%s'", get_gif_error_string(gif), fname);
                    done = true;
                    break;
                }

                while (ext)
                {
                    if (code == GRAPHICS_EXT_FUNC_CODE)
                    {
                        lastDelay = (ext[2] | (ext[3] << 8)) * 10;
                        lastTransparentIdx = (ext[1] & 0x01) ? ext[4] : 0xFF;

                        if(lastDelay == 0)
                        {
                            lastDelay = 1;
                        }
                    }

                    if (DGifGetExtensionNext(gif, &ext) != GIF_OK)
                    {
                        Msg("! [ERROR]: Failed to read next GIF extension (%s) in '%s'", get_gif_error_string(gif), fname);
                        done = true;
                        break;
                    }
                }
                break;
            }
            case GifRecordType::IMAGE_DESC_RECORD_TYPE:
            {
                if (DGifGetImageDesc(gif) != GIF_OK)
                {
                    Msg("! [ERROR]: Failed to read GIF image description (%s) in '%s'", get_gif_error_string(gif), fname);
                    done = true;
                    break;
                }

                const GifWord left = gif->Image.Left;
                const GifWord top = gif->Image.Top;
                const GifWord width = gif->Image.Width;
                const GifWord height = gif->Image.Height;
                ColorMapObject* const colorMap = gif->Image.ColorMap ? gif->Image.ColorMap : gif->SColorMap;
                if (colorMap)
                {
                    VERIFY((left + width) <= gif->SWidth);
                    VERIFY((top + height) <= gif->SHeight);

                    for (GifWord y = 0; y < height; ++y)
                    {
                        if (DGifGetLine(gif, tempLine, width) != GIF_OK)
                        {
                            Msg("! [ERROR]: Failed to read GIF image line (%s) in '%s'", get_gif_error_string(gif), fname);
                            done = true;
                            break;
                        }

                        const GifWord canY = top + y;
                        for (GifWord x = 0; x < width; ++x)
                        {
                            const GifWord canX = left + x;
                            const u8 idx = tempLine[x];
                            if (idx != lastTransparentIdx && idx < colorMap->ColorCount)
                            {
                                const u32 offset = static_cast<u32>((canY * gif->SWidth + canX) * 4);
                                const GifColorType c = colorMap->Colors[idx];
#if (defined(STATIC_RENDERER_R1) || defined(STATIC_RENDERER_R2))
                                tempImage[offset + 0] = c.Blue;
                                tempImage[offset + 1] = c.Green;
                                tempImage[offset + 2] = c.Red;
                                tempImage[offset + 3] = 0xFF;
#else
                                tempImage[offset + 0] = c.Red;
                                tempImage[offset + 1] = c.Green;
                                tempImage[offset + 2] = c.Blue;
                                tempImage[offset + 3] = 0xFF;
#endif
                            }
                        }
                    }

                    if (!done)
                    {
                        u8* const imgData = static_cast<u8*>(Memory.mem_alloc(imageSize));
                        CopyMemory(imgData, tempImage, imageSize);
                        CGIFResource::Image& newImage = out.emplace_back();
                        newImage.data = imgData;
                        newImage.delay = lastDelay;
                    }
                }
                break;
            }
            case GifRecordType::TERMINATE_RECORD_TYPE:
            {
                done = true;
                result = true;
                break;
            }
            default:
            {
                Msg("! [ERROR]: Unexpected GIF record [%s] in '%s'", ToString_GifRecordType(record_type), fname);
                done = true;
                break;
            }
        }
    } while (!done);
    return result;
}



CGIFResource::CGIFResource()
    : m_Images(), m_Width(0), m_Height(0), m_ImageSize(0)
{
    //
}

CGIFResource::~CGIFResource()
{
    for (const Image& info : m_Images)
    {
        Memory.mem_free(const_cast<u8*>(info.data));
    }
}

bool CGIFResource::Load(const char* fname)
{
    VERIFY(m_Images.empty());

    CStreamReader* reader = FS.rs_open(nullptr, fname);
    VERIFY(reader);

    int err = 0;
    GifFileType* const gif = DGifOpen(reader, &Read_GIF_Fn, &err);
    if (!gif)
    {
        const char* errStr = GifErrorString(err);
        if (!errStr) errStr = "<no-desc>";

        Msg("! Can't load gif '%s' (%s)", fname, errStr);
        FS.r_close(reader);
        return false;
    }

    m_Width = static_cast<u32>(gif->SWidth);
    m_Height = static_cast<u32>(gif->SHeight);
    m_ImageSize = m_Width * m_Height * 4;

    const u32 tempMemSize = m_Width + m_ImageSize;
    void* const tempMem = Memory.mem_alloc(tempMemSize);
    u8* const tempImage = static_cast<u8*>(tempMem);
    u8* const tempLine = static_cast<u8*>(tempMem) + m_ImageSize;

    std::memset(tempMem, 0, tempMemSize);
    const bool decodeResult = decode_gif(gif, tempImage, tempLine, m_Images, m_ImageSize, fname);

    DGifCloseFile(gif, nullptr);
    FS.r_close(reader);
    Memory.mem_free(tempMem);
    return decodeResult;
}

const xr_vector<CGIFResource::Image>& CGIFResource::GetImages() const
{
    return m_Images;
}

u32 CGIFResource::GetWidth() const
{
    return m_Width;
}

u32 CGIFResource::GetHeight() const
{
    return m_Height;
}

u32 CGIFResource::GetImageSize() const
{
    return m_ImageSize;
}
