#include "Thumbnails.hpp"
#include "../miniz_extension.hpp"

#include <jpeglib.h>
#include <jerror.h>
#include <vector>

namespace Slic3r::GCodeThumbnails {

    using namespace std::literals;

    struct CompressedPNG : CompressedImageBuffer
    {
        ~CompressedPNG() override { if (data) mz_free(data); }
        std::string_view tag() const override { return "thumbnail"sv; }
    };

    struct CompressedQidi : CompressedImageBuffer
    {
        ~CompressedQidi() override { free(data); }
        std::string_view tag() const override { return "thumbnail_QIDI"sv; }
    };

    std::unique_ptr<CompressedImageBuffer> compress_thumbnail_png(const ThumbnailData& data)
    {
        auto out = std::make_unique<CompressedPNG>();
        out->data = tdefl_write_image_to_png_file_in_memory_ex((const void*)data.pixels.data(), data.width, data.height, 4, &out->size, MZ_DEFAULT_LEVEL, 1);
        return out;
    }

    int Qidi_EncodeStr(unsigned short* fromcolor16, int picw, int pich, unsigned char* outputdata, int outputmaxtsize, int colorsmax);
    
    std::string compress_thumbnail_qidi(const ThumbnailData& data)
    {
        auto out = std::make_unique<CompressedPNG>();
        // BOOST_LOG_TRIVIAL(error) << data.width;
        int width = int(data.width);
        int height = int(data.height);

        if (data.width * data.height > 500 * 500) {
            width = 500;
            height = 500;
        }
        U16 color16[500 * 500];
        // U16 *color16 = new U16[data.width * data.height];
        // for (int i = 0; i < 200*200; i++) color16[i] = 522240;
        unsigned char outputdata[500 * 500 * 10];
        // unsigned char *outputdata = new unsigned char[data.width * data.height * 10];

        std::vector<uint8_t> rgba_pixels(data.pixels.size() * 4);
        size_t               row_size = width * 4;
        for (size_t y = 0; y < height; ++y)
            memcpy(rgba_pixels.data() + y * row_size, data.pixels.data() + y * row_size, row_size);
        const unsigned char* pixels;
        pixels = (const unsigned char*)rgba_pixels.data();
        int rrrr = 0, gggg = 0, bbbb = 0, aaaa = 0, rgb = 0;

        int time = width * height - 1; // 200*200-1;

        for (unsigned int r = 0; r < height; ++r) {
            unsigned int rr = r * width;
            for (unsigned int c = 0; c < width; ++c) {
                unsigned int cc = width - c - 1;
                rrrr = int(pixels[4 * (rr + cc) + 0]) >> 3;
                gggg = int(pixels[4 * (rr + cc) + 1]) >> 2;
                bbbb = int(pixels[4 * (rr + cc) + 2]) >> 3;
                aaaa = int(pixels[4 * (rr + cc) + 3]);
                if (aaaa == 0) {
                    rrrr = 239 >> 3;
                    gggg = 243 >> 2;
                    bbbb = 247 >> 3;
                }
                rgb = (rrrr << 11) | (gggg << 5) | bbbb;
                color16[time--] = rgb;
            }
        }

        int         res = Qidi_EncodeStr(color16, width, height, outputdata, height * width * 10, 1024);
        std::string temp;

        // for (unsigned char i : outputdata) { temp += i; }
        for (unsigned int i = 0; i < sizeof(outputdata); ++i) {
            temp += outputdata[i];
            // unsigned char strr = outputdata[i];
            // temp += strr;
        }
        // out->data = tdefl_write_image_to_png_file_in_memory_ex((const void*)data.pixels.data(), data.width, data.height, 4, &out->size,
        // MZ_DEFAULT_LEVEL, 1);
        return temp;
    }


    std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData& data, GCodeThumbnailsFormat format)
    {
        switch (format) {
        case GCodeThumbnailsFormat::PNG:
        default:
            return compress_thumbnail_png(data);
        }
    }

    typedef struct
    {
        unsigned short colo16;
        unsigned char  A0;
        unsigned char  A1;
        unsigned char  A2;
        unsigned char  res0;
        unsigned short res1;
        unsigned int   qty;
    } U16HEAD;
    typedef struct
    {
        unsigned char  encodever;
        unsigned char  res0;
        unsigned short oncelistqty;
        unsigned int   PicW;
        unsigned int   PicH;
        unsigned int   mark;
        unsigned int   ListDataSize;
        unsigned int   ColorDataSize;
        unsigned int   res1;
        unsigned int   res2;
    } QidiHead3;

    static void colmemmove(unsigned char* dec, unsigned char* src, int lenth)
    {
        if (src < dec) {
            dec += lenth - 1;
            src += lenth - 1;
            while (lenth > 0) {
                *(dec--) = *(src--);
                lenth--;
            }
        }
        else {
            while (lenth > 0) {
                *(dec++) = *(src++);
                lenth--;
            }
        }
    }
    static void colmemcpy(unsigned char* dec, unsigned char* src, int lenth)
    {
        while (lenth > 0) {
            *(dec++) = *(src++);
            lenth--;
        }
    }
    static void colmemset(unsigned char* dec, unsigned char val, int lenth)
    {
        while (lenth > 0) {
            *(dec++) = val;
            lenth--;
        }
    }

    static void ADList0(unsigned short val, U16HEAD* listu16, int* listqty, int maxqty)
    {
        unsigned char A0;
        unsigned char A1;
        unsigned char A2;
        int           qty = *listqty;
        if (qty >= maxqty)
            return;
        for (int i = 0; i < qty; i++) {
            if (listu16[i].colo16 == val) {
                listu16[i].qty++;
                return;
            }
        }
        A0 = (unsigned char)(val >> 11);
        A1 = (unsigned char)((val << 5) >> 10);
        A2 = (unsigned char)((val << 11) >> 11);
        U16HEAD* a = &listu16[qty];
        a->colo16 = val;
        a->A0 = A0;
        a->A1 = A1;
        a->A2 = A2;
        a->qty = 1;
        *listqty = qty + 1;
    }

    static int Byte8bitEncode(
        unsigned short* fromcolor16, unsigned short* listu16, int listqty, int dotsqty, unsigned char* outputdata, int decMaxBytesize)
    {
        unsigned char tid, sid;
        int           dots = 0;
        int           srcindex = 0;
        int           decindex = 0;
        int           lastid = 0;
        int           temp = 0;
        while (dotsqty > 0) {
            dots = 1;
            for (int i = 0; i < (dotsqty - 1); i++) {
                if (fromcolor16[srcindex + i] != fromcolor16[srcindex + i + 1])
                    break;
                dots++;
                if (dots == 255)
                    break;
            }
            temp = 0;
            for (int i = 0; i < listqty; i++) {
                if (listu16[i] == fromcolor16[srcindex]) {
                    temp = i;
                    break;
                }
            }
            tid = (unsigned char)(temp % 32);
            sid = (unsigned char)(temp / 32);
            if (lastid != sid) {
                if (decindex >= decMaxBytesize)
                    goto IL_END;
                outputdata[decindex] = 7;
                outputdata[decindex] <<= 5;
                outputdata[decindex] += sid;
                decindex++;
                lastid = sid;
            }
            if (dots <= 6) {
                if (decindex >= decMaxBytesize)
                    goto IL_END;
                outputdata[decindex] = (unsigned char)dots;
                outputdata[decindex] <<= 5;
                outputdata[decindex] += tid;
                decindex++;
            }
            else {
                if (decindex >= decMaxBytesize)
                    goto IL_END;
                outputdata[decindex] = 0;
                outputdata[decindex] += tid;
                decindex++;
                if (decindex >= decMaxBytesize)
                    goto IL_END;
                outputdata[decindex] = (unsigned char)dots;
                decindex++;
            }
            srcindex += dots;
            dotsqty -= dots;
        }
    IL_END:
        return decindex;
    }

    static int QidiEncode(unsigned short* fromcolor16, int picw, int pich, unsigned char* outputdata, int outputmaxtsize, int colorsmax)
    {
        U16HEAD      l0;
        int          cha0, cha1, cha2, fid, minval;
        ColPicHead3* Head0 = null;
        U16HEAD      Listu16[1024];
        int          ListQty = 0;
        int          enqty = 0;
        int          dotsqty = picw * pich;
        if (colorsmax > 1024)
            colorsmax = 1024;
        for (int i = 0; i < dotsqty; i++) {
            int ch = (int)fromcolor16[i];
            ADList0(ch, Listu16, &ListQty, 1024);
        }

        for (int index = 1; index < ListQty; index++) {
            l0 = Listu16[index];
            for (int i = 0; i < index; i++) {
                if (l0.qty >= Listu16[i].qty) {
                    colmemmove((U8*)&Listu16[i + 1], (U8*)&Listu16[i], (index - i) * sizeof(U16HEAD));
                    colmemcpy((U8*)&Listu16[i], (U8*)&l0, sizeof(U16HEAD));
                    break;
                }
            }
        }
        while (ListQty > colorsmax) {
            l0 = Listu16[ListQty - 1];
            minval = 255;
            fid = -1;
            for (int i = 0; i < colorsmax; i++) {
                cha0 = Listu16[i].A0 - l0.A0;
                if (cha0 < 0)
                    cha0 = 0 - cha0;
                cha1 = Listu16[i].A1 - l0.A1;
                if (cha1 < 0)
                    cha1 = 0 - cha1;
                cha2 = Listu16[i].A2 - l0.A2;
                if (cha2 < 0)
                    cha2 = 0 - cha2;
                int chall = cha0 + cha1 + cha2;
                if (chall < minval) {
                    minval = chall;
                    fid = i;
                }
            }
            for (int i = 0; i < dotsqty; i++) {
                if (fromcolor16[i] == l0.colo16)
                    fromcolor16[i] = Listu16[fid].colo16;
            }
            ListQty = ListQty - 1;
        }
        Head0 = ((ColPicHead3*)outputdata);
        colmemset(outputdata, 0, sizeof(ColPicHead3));
        Head0->encodever = 3;
        Head0->oncelistqty = 0;
        Head0->mark = 0x05DDC33C;
        Head0->ListDataSize = ListQty * 2;
        for (int i = 0; i < ListQty; i++) {
            U16* l0 = (U16*)&outputdata[sizeof(ColPicHead3)];
            l0[i] = Listu16[i].colo16;
        }
        enqty = Byte8bitEncode(fromcolor16, (U16*)&outputdata[sizeof(ColPicHead3)], Head0->ListDataSize >> 1, dotsqty,
            &outputdata[sizeof(ColPicHead3) + Head0->ListDataSize],
            outputmaxtsize - sizeof(ColPicHead3) - Head0->ListDataSize);
        Head0->ColorDataSize = enqty;
        Head0->PicW = picw;
        Head0->PicH = pich;
        return sizeof(ColPicHead3) + Head0->ListDataSize + Head0->ColorDataSize;
    }

    int Qidi_EncodeStr(unsigned short* fromcolor16, int picw, int pich, unsigned char* outputdata, int outputmaxtsize, int colorsmax)
    {
        int qty = 0;
        int temp = 0;
        int strindex = 0;
        int hexindex = 0;
        U8  TempBytes[4];
        qty = QidiEncode(fromcolor16, picw, pich, outputdata, outputmaxtsize, colorsmax);
        if (qty == 0)
            return 0;
        temp = 3 - (qty % 3);
        while (temp > 0) {
            outputdata[qty] = 0;
            qty++;
            temp--;
        }
        if ((qty * 4 / 3) >= outputmaxtsize)
            return 0;
        hexindex = qty;
        strindex = (qty * 4 / 3);
        while (hexindex > 0) {
            hexindex -= 3;
            strindex -= 4;

            TempBytes[0] = (U8)(outputdata[hexindex] >> 2);
            TempBytes[1] = (U8)(outputdata[hexindex] & 3);
            TempBytes[1] <<= 4;
            TempBytes[1] += ((U8)(outputdata[hexindex + 1] >> 4));
            TempBytes[2] = (U8)(outputdata[hexindex + 1] & 15);
            TempBytes[2] <<= 2;
            TempBytes[2] += ((U8)(outputdata[hexindex + 2] >> 6));
            TempBytes[3] = (U8)(outputdata[hexindex + 2] & 63);

            TempBytes[0] += 48;
            if (TempBytes[0] == (U8)'\\')
                TempBytes[0] = 126;
            TempBytes[0 + 1] += 48;
            if (TempBytes[0 + 1] == (U8)'\\')
                TempBytes[0 + 1] = 126;
            TempBytes[0 + 2] += 48;
            if (TempBytes[0 + 2] == (U8)'\\')
                TempBytes[0 + 2] = 126;
            TempBytes[0 + 3] += 48;
            if (TempBytes[0 + 3] == (U8)'\\')
                TempBytes[0 + 3] = 126;

            outputdata[strindex] = TempBytes[0];
            outputdata[strindex + 1] = TempBytes[1];
            outputdata[strindex + 2] = TempBytes[2];
            outputdata[strindex + 3] = TempBytes[3];
        }
        qty = qty * 4 / 3;
        outputdata[qty] = 0;
        return qty;
    }
    std::string compress_qidi_thumbnail(const ThumbnailData& data, GCodeThumbnailsFormat format) { return compress_thumbnail_qidi(data); }
    std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const std::string& thumbnails_string, const std::string_view def_ext /*= "PNG"sv*/)
    {
        if (thumbnails_string.empty())
            return {};

        std::istringstream is(thumbnails_string);
        std::string point_str;

        ThumbnailErrors errors;

        GCodeThumbnailDefinitionsList thumbnails_list;
        while (std::getline(is, point_str, ',')) {
            Vec2d point(Vec2d::Zero());
            GCodeThumbnailsFormat format;
            std::istringstream iss(point_str);
            std::string coord_str;
            if (std::getline(iss, coord_str, 'x') && !coord_str.empty()) {
                std::istringstream(coord_str) >> point(0);
                if (std::getline(iss, coord_str, '/') && !coord_str.empty()) {
                    std::istringstream(coord_str) >> point(1);

                    if (0 < point(0) && point(0) < 1000 && 0 < point(1) && point(1) < 1000) {
                        std::string ext_str;
                        std::getline(iss, ext_str, '/');
                        if (ext_str.empty())
                            ext_str = def_ext.empty() ? "PNG"sv : def_ext;
                        boost::to_upper(ext_str);
                        if (!ConfigOptionEnum<GCodeThumbnailsFormat>::from_string(ext_str, format)) {
                            format = GCodeThumbnailsFormat::PNG;
                            errors = enum_bitmask(errors | ThumbnailError::InvalidExt);
                        }

                        thumbnails_list.emplace_back(std::make_pair(format, point));
                    }
                    else
                        errors = enum_bitmask(errors | ThumbnailError::OutOfRange);
                    continue;
                }
            }
            errors = enum_bitmask(errors | ThumbnailError::InvalidVal);
        }

        return std::make_pair(std::move(thumbnails_list), errors);
    }

    std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const ConfigBase& config)
    {

        if (const auto thumbnails_value = config.option<ConfigOptionString>("thumbnail_size"))
            return make_and_check_thumbnail_list(thumbnails_value->value);

        return {};
    }

    std::string get_error_string(const ThumbnailErrors& errors)
    {
        std::string error_str;

        if (errors.has(ThumbnailError::InvalidVal))
            error_str += "\n - " + format("Invalid input format. Expected vector of dimensions in the following format: \"%1%\"", "XxY/EXT, XxY/EXT, ...");
        if (errors.has(ThumbnailError::OutOfRange))
            error_str += "\n - Input value is out of range";
        if (errors.has(ThumbnailError::InvalidExt))
            error_str += "\n - Some extension in the input is invalid";

        return error_str;
    }

} // namespace Slic3r::GCodeThumbnails